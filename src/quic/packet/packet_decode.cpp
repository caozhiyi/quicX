#include "common/decode/decode.h"
#include "common/log/log.h"

#include "quic/common/version.h"
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
            // RFC 9000 §12.2: If we already decoded at least one packet from this
            // datagram, treat remaining unparseable bytes as trailing garbage and
            // return success.
            if (!packets.empty()) {
                common::LOG_DEBUG("ignoring %zu trailing bytes after %zu successfully decoded packet(s)",
                    len, packets.size());
                buffer->MoveReadPt(buffer->GetDataLength());
                return true;
            }
            common::LOG_ERROR("decode header flag failed.");
            return false;
        }

        std::shared_ptr<IPacket> packet;
        if (flag.GetHeaderType() == PacketHeaderType::kShortHeader) {
            // RFC 9000 §12.2: A short header (1-RTT) packet can only appear as
            // the last packet in a coalesced datagram.  If we already decoded one
            // or more long-header packets and the remaining bytes look like a
            // short-header packet whose fixed bit is not set, the data is likely
            // encrypted padding or a coalesced packet we cannot yet decrypt
            // (we don't have 1-RTT keys during the handshake).  Tolerate this
            // gracefully instead of discarding all previously-decoded packets.
            packet = std::make_shared<Rtt1Packet>(flag.GetFlag());

        } else {
            // For long header packets, peek at the Version field without consuming it
            if (buffer->GetDataLength() >= 4) {
                auto span = buffer->GetReadableSpan();
                uint32_t version = 0;
                common::FixedDecodeUint32(span.GetStart(), span.GetEnd(), version);

                if (version == 0) {
                    // Version Negotiation packet (RFC 9000 Section 17.2.1)
                    common::LOG_DEBUG("get packet type:version_negotiation (version=0)");
                    packet = std::make_shared<VersionNegotiationPacket>(flag.GetFlag());

                } else if (!VersionCheck(version)) {
                    // Unknown version: we cannot parse the packet body because
                    // future versions may change the packet format.
                    // Parse only the Long Header (Version + DCID + SCID) so the
                    // upper layer can respond with a Version Negotiation packet.
                    // RFC 9000 Section 6.1: "If the version is not acceptable,
                    // the server responds with a Version Negotiation packet."
                    common::LOG_WARN("unsupported QUIC version 0x%08x, will send Version Negotiation", version);
                    auto init_pkt = std::make_shared<InitPacket>(flag.GetFlag());
                    // Decode only the Long Header (version, DCID, SCID)
                    if (!init_pkt->GetHeader()->DecodeHeader(buffer, false)) {
                        common::LOG_ERROR("failed to decode header for unsupported version");
                        return false;
                    }
                    // Consume the entire remaining buffer since we can't parse
                    // the body of an unknown version packet
                    buffer->MoveReadPt(buffer->GetDataLength());
                    packets.emplace_back(init_pkt);
                    return true;

                } else {
                    // Supported version, determine type from packet type bits.
                    // RFC 9369 §3.2: QUIC v2 remaps the Long Header Packet Type
                    // wire bits. Interpret the 2-bit field according to the
                    // actual version so that the rest of the stack can stay
                    // version-agnostic (internally we always use v1 enum
                    // values as the canonical PacketType representation).
                    uint8_t wire_type_bits = flag.GetLongHeaderFlag().GetPacketType();
                    PacketType logical_type = MapWireToPacketType(wire_type_bits, version);
                    common::LOG_DEBUG("get packet type:%s (wire_bits=%u, version=0x%08x)",
                        PacketTypeToString(logical_type), wire_type_bits, version);
                    // Pass the original wire flag byte to the packet subclass.
                    // The in-memory packet_type_ bitfield therefore carries the
                    // wire bits (which can differ from the v1 enum under
                    // QUICv2); LongHeader::GetPacketType() version-decodes it.
                    uint8_t wire_flag = flag.GetFlag();
                    switch (logical_type) {
                        case PacketType::kInitialPacketType:
                            packet = std::make_shared<InitPacket>(wire_flag);
                            break;
                        case PacketType::k0RttPacketType:
                            packet = std::make_shared<Rtt0Packet>(wire_flag);
                            break;
                        case PacketType::kHandshakePacketType:
                            packet = std::make_shared<HandshakePacket>(wire_flag);
                            break;
                        case PacketType::kRetryPacketType:
                            packet = std::make_shared<RetryPacket>(wire_flag);
                            break;
                        default:
                            common::LOG_ERROR("unknown packet type. wire_bits:%u", wire_type_bits);
                            if (!packets.empty()) {
                                buffer->MoveReadPt(buffer->GetDataLength());
                                return true;
                            }
                            return false;
                    }
                }
            } else {
                // Not enough data for a version field
                if (!packets.empty()) {
                    common::LOG_DEBUG("ignoring %d trailing bytes (insufficient for version field) "
                        "after %zu decoded packet(s)", buffer->GetDataLength(), packets.size());
                    buffer->MoveReadPt(buffer->GetDataLength());
                    return true;
                }
                common::LOG_ERROR("insufficient data for version field. remaining:%d", buffer->GetDataLength());
                return false;
            }
        }
        if (!packet->DecodeWithoutCrypto(buffer)) {
            // RFC 9000 §12.2: "Each QUIC packet can be coalesced into a single
            // UDP datagram... Implementations SHOULD be able to process coalesced
            // packets."  When a subsequent packet in a coalesced datagram fails
            // to decode (e.g., because the encryption keys are not yet available
            // or the packet is from a different encryption level), we keep the
            // packets that were successfully decoded and discard the rest.
            if (!packets.empty()) {
                common::LOG_DEBUG("failed to decode coalesced packet #%zu (remaining %zu bytes), "
                    "keeping %zu previously decoded packet(s)",
                    packets.size() + 1, buffer->GetDataLength(), packets.size());
                buffer->MoveReadPt(buffer->GetDataLength());
                return true;
            }
            common::LOG_ERROR("decode header packet failed.");
            return false;
        }
        packets.emplace_back(packet);
    }

    return true;
}

}  // namespace quic
}  // namespace quicx
