#include "common/log/log.h"
#include "common/util/time.h"
#include "quic/connection/util.h"
#include "quic/packet/init_packet.h"
#include "quic/frame/padding_frame.h"
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
    send_control_(timer) {
    
}

SendManager::~SendManager() {

}

bool SendManager::IsAllSendDone() {
    return wait_frame_list_.empty() && active_send_stream_set_.empty();
}

void SendManager::ToSendFrame(std::shared_ptr<IFrame> frame) {
    wait_frame_list_.emplace_front(frame);
}

void SendManager::ActiveStream(std::shared_ptr<IStream> stream) {
    active_send_stream_set_.insert(stream);
}

bool SendManager::GetSendData(std::shared_ptr<common::IBuffer> buffer, uint8_t encrypto_level, std::shared_ptr<ICryptographer> cryptographer) {
    uint32_t can_send_size = 1500; // TODO: set to mtu size
    send_control_.CanSend(common::UTCTimeMsec(), can_send_size);
    if (can_send_size == 0) {
        common::LOG_WARN("congestion control send data limited.");
        return true;
    }

    // firstly, send resend packet
    if (send_control_.NeedReSend()) {
        auto lost_list = send_control_.GetLostPacket();
        auto packet = lost_list.front();
        lost_list.pop_front();

        return PacketInit(packet, buffer);

    } else {
        // secondly, send new packet
        // check flow control
        std::shared_ptr<IFrame> frame;
        if (!flow_control_->CheckLocalSendDataLimit(can_send_size, frame)) {
            common::LOG_WARN("local send data limited.");
            return false;
        }
        if (frame) {
            ToSendFrame(frame);
        }
    
        FixBufferFrameVisitor frame_visitor(1450); // TODO put 1450 to config
        frame_visitor.SetStreamDataSizeLimit(can_send_size);
        auto packet = MakePacket(&frame_visitor, encrypto_level, cryptographer);
        if (!packet) {
            common::LOG_ERROR("make packet failed.");
            return false;
        }
        return PacketInit(packet, buffer);
    }
}

void SendManager::OnPacketAck(PacketNumberSpace ns, std::shared_ptr<IFrame> frame) {
    send_control_.OnPacketAck(common::UTCTimeMsec(), ns, frame);
}

std::shared_ptr<IPacket> SendManager::MakePacket(IFrameVisitor* visitor, uint8_t encrypto_level, std::shared_ptr<ICryptographer> cryptographer) {
    std::shared_ptr<IPacket> packet;
    // priority sending frames of connection
    for (auto iter = wait_frame_list_.begin(); iter != wait_frame_list_.end();) {
        if (visitor->HandleFrame(*iter)) {
            iter = wait_frame_list_.erase(iter);

        } else {
            common::LOG_ERROR("handle frame failed.");
            return nullptr;
        }
    }

    bool need_break = false;
    while (true) {
        if (active_send_stream_set_.empty() || need_break) {
            break;
        }
        
        // then sending frames of stream
        for (auto iter = active_send_stream_set_.begin(); iter != active_send_stream_set_.end();) {
            auto ret = (*iter)->TrySendData(visitor);
            if (ret == IStream::TSR_SUCCESS) {
                iter = active_send_stream_set_.erase(iter);
    
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
            // add padding frame
            auto padding_frame = std::make_shared<PaddingFrame>();
            padding_frame->SetPaddingLength(1300 - visitor->GetBuffer()->GetDataLength());
            visitor->HandleFrame(padding_frame);
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
        auto cid = local_conn_id_manager_->GetCurrentID();
        ((LongHeader*)packet->GetHeader())->SetSourceConnectionId(cid.id_, cid.len_);
    }

    auto cid = remote_conn_id_manager_->GetCurrentID();
    packet->GetHeader()->SetDestinationConnectionId(cid.id_, cid.len_);
    packet->SetPayload(visitor->GetBuffer()->GetReadSpan());
    packet->SetCryptographer(cryptographer);

    return packet;
}

bool SendManager::PacketInit(std::shared_ptr<IPacket>& packet, std::shared_ptr<common::IBuffer> buffer) {
    // make packet numer
    uint64_t pkt_number = pakcet_number_.NextPakcetNumber(CryptoLevel2PacketNumberSpace(packet->GetCryptoLevel()));
    packet->SetPacketNumber(pkt_number);
    packet->GetHeader()->SetPacketNumberLength(PacketNumber::GetPacketNumberLength(pkt_number));

    if (!packet->Encode(buffer)) {
        common::LOG_ERROR("encode packet error");
        return false;
    }

    send_control_.OnPacketSend(common::UTCTimeMsec(), packet, buffer->GetDataLength());
    return true;
}

}
}