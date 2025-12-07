#include "common/decode/decode.h"
#include "common/log/log.h"

#include "quic/packet/handshake_packet.h"
#include "quic/packet/header/header_flag.h"
#include "quic/packet/init_packet.h"
#include "quic/packet/packet_decode.h"
#include "quic/packet/retry_packet.h"
#include "quic/packet/rtt_0_packet.h"
#include "quic/packet/rtt_1_packet.h"
#include "quic/packet/version_negotiation_packet.h"

namespace quicx {
namespace quic {

bool DecodePackets(std::shared_ptr<common::IBuffer> buffer, std::vector<std::shared_ptr<IPacket>>& packets) {
    if (!buffer) {
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
        if (flag.GetHeaderType() == PacketHeaderType::kShortHeader) {
            packet = std::make_shared<Rtt1Packet>(flag.GetFlag());

        } else {
            // For long header packets, check if it's a Version Negotiation packet
            // Version Negotiation packets have Version field = 0 (RFC 9000 Section 17.2.1)
            // We need to peek at the Version field without consuming it
            if (buffer->GetDataLength() >= 4) {
                auto span = buffer->GetReadableSpan();
                uint32_t version = 0;
                common::FixedDecodeUint32(span.GetStart(), span.GetEnd(), version);

                if (version == 0) {
                    // Version Negotiation packet
                    common::LOG_DEBUG("get packet type:version_negotiation (version=0)");
                    packet = std::make_shared<VersionNegotiationPacket>(flag.GetFlag());
                } else {
                    // Not Version Negotiation, determine type from packet type bits
                    common::LOG_DEBUG("get packet type:%s", PacketTypeToString(flag.GetPacketType()));
                    switch (flag.GetPacketType()) {
                        case PacketType::kInitialPacketType:
                            packet = std::make_shared<InitPacket>(flag.GetFlag());
                            break;
                        case PacketType::k0RttPacketType:
                            packet = std::make_shared<Rtt0Packet>(flag.GetFlag());
                            break;
                        case PacketType::kHandshakePacketType:
                            packet = std::make_shared<HandshakePacket>(flag.GetFlag());
                            break;
                        case PacketType::kRetryPacketType:
                            packet = std::make_shared<RetryPacket>(flag.GetFlag());
                            break;
                        default:
                            common::LOG_ERROR("unknow packet type. type:%d", flag.GetPacketType());
                            return false;
                    }
                }
            } else {
                common::LOG_ERROR("insufficient data for version field. remaining:%d", buffer->GetDataLength());
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

}  // namespace quic
}  // namespace quicx
