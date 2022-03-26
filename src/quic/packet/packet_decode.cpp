#include "common/log/log.h"
#include "quic/packet/header_flag.h"
#include "quic/packet/long_header.h"
#include "quic/packet/init_packet.h"
#include "quic/packet/rtt_1_packet.h"
#include "quic/packet/rtt_0_packet.h"
#include "quic/packet/retry_packet.h"
#include "quic/packet/short_header.h"
#include "quic/packet/packet_decode.h"
#include "quic/packet/hand_shake_packet.h"
#include "quic/packet/version_negotiation_packet.h"

namespace quicx {

bool DecodePackets(std::shared_ptr<IBufferReadOnly> buffer, std::vector<std::shared_ptr<IPacket>>& packets) {
    if(!buffer) {
        return false;
    }

    HeaderFlag flag;
    while (buffer->GetCanReadLength() > 0) {
        if (!flag.Decode(buffer)) {
            LOG_ERROR("decode header flag failed.");
            return false;
        }

        std::shared_ptr<IPacket> packet;
        if (flag.IsShortHeaderFlag()) {
            std::shared_ptr<ShortHeader> header = std::make_shared<ShortHeader>(flag);
            if (header->Decode(buffer)) {
                LOG_ERROR("decode short header failed.");
                return false;
            }
            
            packet = std::make_shared<Rtt1Packet>(header);
            if (packet->Decode(buffer)) {
                LOG_ERROR("decode 1 rtt packet failed.");
                return false;
            }
            packets.emplace_back(packet);
            continue;
        }
            
        std::shared_ptr<LongHeader> header = std::make_shared<LongHeader>(flag);
        if (header->Decode(buffer)) {
            LOG_ERROR("decode long header failed.");
            return false;
        }

        switch (header->GetPacketType())
        {
        case PT_INITIAL:
            packet = std::make_shared<InitPacket>(header);
            break;
        case PT_0RTT:
            packet = std::make_shared<Rtt0Packet>(header);
            break;
        case PT_HANDSHAKE:
            packet = std::make_shared<HandShakePacket>(header);
            break;
        case PT_RETRY:
            packet = std::make_shared<RetryPacket>(header);
            break;
        case PT_NEGOTIATION:
            packet = std::make_shared<VersionNegotiationPacket>(header);
            break;
        default:
            LOG_ERROR("unknow packet type. type:%d", header->GetPacketType());
        }
        
        if (packet->Decode(buffer)) {
            LOG_ERROR("decode long header packet failed.");
            return false;
        }
        packets.emplace_back(packet);
    }

    return true;
}

}

