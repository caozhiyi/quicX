#include "common/log/log.h"
#include "quic/packet/init_packet.h"
#include "quic/packet/rtt_1_packet.h"
#include "quic/packet/rtt_0_packet.h"
#include "quic/packet/retry_packet.h"
#include "quic/packet/packet_decode.h"
#include "quic/packet/handshake_packet.h"
#include "quic/packet/header/header_flag.h"
#include "quic/packet/version_negotiation_packet.h"

namespace quicx {
namespace quic {

bool DecodePackets(std::shared_ptr<common::IBufferRead> buffer, std::vector<std::shared_ptr<IPacket>>& packets) {
    if(!buffer) {
        return false;
    }

    HeaderFlag flag;
    while (buffer->GetDataLength() > 0) {
        auto len = buffer->GetDataLength();
        if (!flag.DecodeFlag(buffer)) {
            common::LOG_ERROR("decode header flag failed.");
            return false;
        }

        std::shared_ptr<IPacket> packet;
        if (flag.GetHeaderType() == PHT_SHORT_HEADER) {
            packet = std::make_shared<Rtt1Packet>(flag.GetFlag());
           
        } else {
            common::LOG_DEBUG("get packet type:%s", PacketTypeToString(flag.GetPacketType()));
            switch (flag.GetPacketType())
            {
            case PT_INITIAL:
                packet = std::make_shared<InitPacket>(flag.GetFlag());
                break;
            case PT_0RTT:
                packet = std::make_shared<Rtt0Packet>(flag.GetFlag());
                break;
            case PT_HANDSHAKE:
                packet = std::make_shared<HandshakePacket>(flag.GetFlag());
                break;
            case PT_RETRY:
                packet = std::make_shared<RetryPacket>(flag.GetFlag());
                break;
            case PT_NEGOTIATION:
                packet = std::make_shared<VersionNegotiationPacket>(flag.GetFlag());
                break;
            default:
                common::LOG_ERROR("unknow packet type. type:%d", flag.GetPacketType());
                return false;
            }
        }
        if (!packet->DecodeWithoutCrypto(buffer)) {
            common::LOG_ERROR("decode header packet failed.");
            return false;
        }
        packets.emplace_back(packet);
    }

    return true;
}

}
}

