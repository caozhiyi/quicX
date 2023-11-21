#include "common/log/log.h"
#include "common/util/time.h"
#include "quic/connection/util.h"
#include "quic/packet/init_packet.h"
#include "quic/packet/retry_packet.h"
#include "quic/packet/rtt_0_packet.h"
#include "quic/packet/rtt_1_packet.h"
#include "quic/packet/handshake_packet.h"
#include "quic/packet/header/long_header.h"
#include "quic/stream/fix_buffer_frame_visitor.h"
#include "quic/connection/controler/send_manager.h"

namespace quicx {
namespace quic {

SendManager::SendManager(std::shared_ptr<common::ITimer> timer): 
    _send_control(timer) {
    
}

SendManager::~SendManager() {

}

void SendManager::AddFrame(std::shared_ptr<IFrame> frame) {
    _wait_frame_list.emplace_front(frame);
}

void SendManager::AddActiveStream(std::shared_ptr<IStream> stream) {
    _active_send_stream_set.insert(stream);
}

bool SendManager::GetSendData(std::shared_ptr<common::IBuffer> buffer, uint8_t encrypto_level, std::shared_ptr<ICryptographer> cryptographer) {
    // firstly, send resend packet
    if (_send_control.NeedReSend()) {
        auto lost_list = _send_control.GetLostPacket();
        auto packet = lost_list.front();
        lost_list.pop_front();

        return PacketInit(packet, buffer);

    } else {
        // secondly, send new packet
        // check flow control
        uint32_t can_send_size;
        std::shared_ptr<IFrame> frame;
        if (!_flow_control->CheckLocalSendDataLimit(can_send_size, frame)) {
            return false;
        }
        if (frame) {
            AddFrame(frame);
        }
    
        FixBufferFrameVisitor frame_visitor(1450); // TODO put 1450 to config
        frame_visitor.SetStreamDataSizeLimit(can_send_size);
        auto packet = MakePacket(&frame_visitor, encrypto_level, cryptographer);
        if (!packet) {
            return false;
        }
        return PacketInit(packet, buffer);
    }
}

void SendManager::OnPacketAck(PacketNumberSpace ns, std::shared_ptr<IFrame> frame) {
    _send_control.OnPacketAck(common::UTCTimeMsec(), ns, frame);
}

std::shared_ptr<IPacket> SendManager::MakePacket(IFrameVisitor* visitor, uint8_t encrypto_level, std::shared_ptr<ICryptographer> cryptographer) {
    std::shared_ptr<IPacket> packet;
    // priority sending frames of connection
    for (auto iter = _wait_frame_list.begin(); iter != _wait_frame_list.end();) {
        if (visitor->HandleFrame(*iter)) {
            iter = _wait_frame_list.erase(iter);

        } else {
            return nullptr;
        }
    }

    bool need_break = false;
    while (true) {
        if (_active_send_stream_set.empty() || need_break) {
            break;
        }
        
        // then sending frames of stream
        for (auto iter = _active_send_stream_set.begin(); iter != _active_send_stream_set.end();) {
            auto ret = (*iter)->TrySendData(visitor);
            if (ret == IStream::TSR_SUCCESS) {
                iter = _active_send_stream_set.erase(iter);
    
            } else if (ret == IStream::TSR_FAILED) {
                common::LOG_ERROR("get stream send data failed.");
                return nullptr;
    
            } else if (ret == IStream::TSR_BREAK) {
                need_break = true;
                break;
            }
        }
    }

    if (visitor->GetBuffer()->GetDataLength() == 0) {
        common::LOG_INFO("there is no data to send.");
        return nullptr;
    }

    switch (encrypto_level) {
        case EL_INITIAL: {
            packet = std::make_shared<InitPacket>();
            break;
        }
        case EL_EARLY_DATA: {
            packet = std::make_shared<Rtt0Packet>();
            break;
        }
        case EL_HANDSHAKE: {
            packet = std::make_shared<HandshakePacket>();
            break;
        }
        case EL_APPLICATION: {
            packet = std::make_shared<Rtt1Packet>();
            break;
        }
    }

    auto header = packet->GetHeader();
    if (header->GetHeaderType() == PHT_LONG_HEADER) {
        auto cid = _local_conn_id_manager->GetCurrentID();
        ((LongHeader*)packet->GetHeader())->SetSourceConnectionId(cid._id, cid._len);
    }

    auto cid = _remote_conn_id_manager->GetCurrentID();
    packet->GetHeader()->SetDestinationConnectionId(cid._id, cid._len);
    packet->SetPayload(visitor->GetBuffer()->GetReadSpan());
    packet->SetCryptographer(cryptographer);
    return packet;
}

bool SendManager::PacketInit(std::shared_ptr<IPacket>& packet, std::shared_ptr<common::IBuffer> buffer) {
    // make packet numer
    uint64_t pkt_number = _pakcet_number.NextPakcetNumber(CryptoLevel2PacketNumberSpace(packet->GetCryptoLevel()));
    packet->SetPacketNumber(pkt_number);
    packet->GetHeader()->SetPacketNumberLength(PacketNumber::GetPacketNumberLength(pkt_number));

    if (!packet->Encode(buffer)) {
        common::LOG_ERROR("encode packet error");
        return false;
    }

    _send_control.OnPacketSend(common::UTCTimeMsec(), packet);
    return true;
}

}
}