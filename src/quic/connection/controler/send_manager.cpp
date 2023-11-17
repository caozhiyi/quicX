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

SendManager::SendManager(std::shared_ptr<ITimer> timer): 
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

bool SendManager::GetSendData(std::shared_ptr<IBuffer> buffer, uint8_t encrypto_level, std::shared_ptr<ICryptographer> cryptographer) {
    std::shared_ptr<IPacket> packet;
    // firstly, send resend packet
    if (_send_control.NeedReSend()) {
        auto lost_list = _send_control.GetLostPacket();
        packet = lost_list.front();
        lost_list.pop_front();

    // secondly, send new packet
    } else {
        // check flow control
        uint32_t can_send_size;
        std::shared_ptr<IFrame> frame;
        if (!_flow_control->CheckLocalSendDataLimit(can_send_size, frame)) {
            return false;
        }
        if (frame) {
            AddFrame(frame);
        }

        packet = MakePacket(can_send_size, encrypto_level, cryptographer);
        if (!packet) {
            return false;
        }
    }
    
    // make packet numer
    uint64_t pkt_number = _pakcet_number.NextPakcetNumber(CryptoLevel2PacketNumberSpace(packet->GetCryptoLevel()));
    packet->SetPacketNumber(pkt_number);
    packet->GetHeader()->SetPacketNumberLength(PacketNumber::GetPacketNumberLength(pkt_number));

    if (!packet->Encode(buffer)) {
        LOG_ERROR("encode packet error");
        return false;
    }

    _send_control.OnPacketSend(UTCTimeMsec(), packet);
    return true;
}

void SendManager::OnPacketAck(PacketNumberSpace ns, std::shared_ptr<IFrame> frame) {
    _send_control.OnPacketAck(UTCTimeMsec(), ns, frame);
}

std::shared_ptr<IPacket> SendManager::MakePacket(uint32_t can_send_size, uint8_t encrypto_level, std::shared_ptr<ICryptographer> cryptographer) {
    std::shared_ptr<IPacket> packet;
    // TODO put 1450 to config
    FixBufferFrameVisitor frame_visitor(1450);
    frame_visitor.SetStreamDataSizeLimit(can_send_size);
    // priority sending frames of connection
    for (auto iter = _wait_frame_list.begin(); iter != _wait_frame_list.end();) {
        if (frame_visitor.HandleFrame(*iter)) {
            iter = _wait_frame_list.erase(iter);

        } else {
            return nullptr;
        }
    }

    while (true) {
        if (_active_send_stream_set.empty()) {
            break;
        }
        
        // then sending frames of stream
        for (auto iter = _active_send_stream_set.begin(); iter != _active_send_stream_set.end();) {
            auto ret = (*iter)->TrySendData(&frame_visitor);
            if (ret == IStream::TSR_SUCCESS) {
                iter = _active_send_stream_set.erase(iter);
    
            } else if (ret == IStream::TSR_FAILED) {
                LOG_ERROR("get stream send data failed.");
                return nullptr;
    
            } else if (ret == IStream::TSR_BREAK) {
                iter = _active_send_stream_set.erase(iter);
                break;
            }
        }
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
    packet->SetPayload(frame_visitor.GetBuffer()->GetReadSpan());
    packet->SetCryptographer(cryptographer);
    return packet;
}

}