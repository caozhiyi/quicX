#include "quic/connection/packet_builder.h"

#include "common/buffer/single_block_buffer.h"
#include "common/log/log.h"
#include "common/util/time.h"

#include "quic/common/version.h"
#include "quic/connection/connection_id_manager.h"
#include "quic/connection/connection_stream_manager.h"
#include "quic/connection/controler/send_control.h"
#include "quic/connection/util.h"
#include "quic/frame/padding_frame.h"
#include "quic/packet/handshake_packet.h"
#include "quic/packet/header/long_header.h"
#include "quic/packet/init_packet.h"
#include "quic/packet/packet_number.h"
#include "quic/packet/rtt_0_packet.h"
#include "quic/packet/rtt_1_packet.h"
#include "quic/stream/fix_buffer_frame_visitor.h"
#include "quic/stream/if_frame_visitor.h"

namespace quicx {
namespace quic {

PacketBuilder::BuildResult PacketBuilder::BuildPacket(const BuildContext& ctx) {
    BuildResult result;

    // Validate required parameters
    if (!ctx.cryptographer) {
        result.error_message = "cryptographer is null";
        LOG_ERROR("PacketBuilder::BuildPacket: %s", result.error_message.c_str());
        return result;
    }

    if (!ctx.frame_visitor) {
        result.error_message = "frame_visitor is null";
        LOG_ERROR("PacketBuilder::BuildPacket: %s", result.error_message.c_str());
        return result;
    }

    if (!ctx.local_cid_manager || !ctx.remote_cid_manager) {
        result.error_message = "connection ID managers are null";
        LOG_ERROR("PacketBuilder::BuildPacket: %s", result.error_message.c_str());
        return result;
    }

    // Create packet based on encryption level
    auto packet = CreatePacketByLevel(ctx.encryption_level);
    if (!packet) {
        result.error_message = "failed to create packet for encryption level";
        LOG_ERROR("PacketBuilder::BuildPacket: %s %d", result.error_message.c_str(), ctx.encryption_level);
        return result;
    }

    // Handle Initial packet requirements (token and padding)
    if (ctx.encryption_level == kInitial) {
        HandleInitialPacketRequirements(packet, ctx);
    }

    // Set connection IDs (source for long headers, destination for all)
    SetConnectionIDs(packet, ctx.local_cid_manager, ctx.remote_cid_manager);

    // Set version for long headers (use context version, or default if not specified)
    auto header = packet->GetHeader();
    if (header->GetHeaderType() == PacketHeaderType::kLongHeader) {
        uint32_t version = ctx.quic_version != 0 ? ctx.quic_version : kQuicVersions[0];
        ((LongHeader*)header)->SetVersion(version);
    }

    // Set payload from frame visitor
    packet->SetPayload(ctx.frame_visitor->GetBuffer()->GetSharedReadableSpan());

    // Set cryptographer
    packet->SetCryptographer(ctx.cryptographer);

    // If packet number is provided, set it now
    if (ctx.packet_number != 0) {
        packet->SetPacketNumber(ctx.packet_number);
        header->SetPacketNumberLength(PacketNumber::GetPacketNumberLength(ctx.packet_number));
        LOG_DEBUG("PacketBuilder::BuildPacket: set packet number %llu, length=%u", ctx.packet_number,
            header->GetPacketNumberLength());
    }

    result.success = true;
    result.packet = packet;
    LOG_DEBUG("PacketBuilder::BuildPacket: successfully built packet at level %d", ctx.encryption_level);
    return result;
}

std::shared_ptr<IPacket> PacketBuilder::CreatePacketByLevel(EncryptionLevel level) {
    switch (level) {
        case kInitial: {
            auto packet = std::make_shared<InitPacket>();
            return packet;
        }
        case kHandshake: {
            auto packet = std::make_shared<HandshakePacket>();
            return packet;
        }
        case kEarlyData: {
            auto packet = std::make_shared<Rtt0Packet>();
            return packet;
        }
        case kApplication: {
            auto packet = std::make_shared<Rtt1Packet>();
            return packet;
        }
        default:
            LOG_ERROR("PacketBuilder::CreatePacketByLevel: invalid encryption level %d", level);
            return nullptr;
    }
}

void PacketBuilder::SetConnectionIDs(const std::shared_ptr<IPacket>& packet, ConnectionIDManager* local_cid_manager,
    ConnectionIDManager* remote_cid_manager) {
    auto header = packet->GetHeader();

    // Set source CID for long headers
    if (header->GetHeaderType() == PacketHeaderType::kLongHeader) {
        auto local_cid = local_cid_manager->GetCurrentID();
        ((LongHeader*)header)->SetSourceConnectionId(local_cid.GetID(), local_cid.GetLength());
        LOG_DEBUG("PacketBuilder::SetConnectionIDs: set source CID, length=%u, hash=%llu",
            local_cid.GetLength(), local_cid.Hash());
    }

    // Set destination CID for all packets
    auto remote_cid = remote_cid_manager->GetCurrentID();
    header->SetDestinationConnectionId(remote_cid.GetID(), remote_cid.GetLength());
}

void PacketBuilder::HandleInitialPacketRequirements(const std::shared_ptr<IPacket>& packet, const BuildContext& ctx) {
    auto init_packet = std::dynamic_pointer_cast<InitPacket>(packet);
    if (!init_packet) {
        LOG_ERROR("PacketBuilder::HandleInitialPacketRequirements: packet is not InitPacket");
        return;
    }

    // Set token if provided
    if (ctx.token_data && ctx.token_length > 0) {
        init_packet->SetToken(const_cast<uint8_t*>(ctx.token_data), ctx.token_length);
        LOG_DEBUG("PacketBuilder::HandleInitialPacketRequirements: set token of length %zu", ctx.token_length);
    }

    // Add padding if requested
    // RFC 9000 Section 14.1: Initial packets MUST be at least 1200 bytes
    if (ctx.add_padding) {
        uint32_t current_size = ctx.frame_visitor->GetBuffer()->GetDataLength();
        uint32_t target_size = 1200;

        if (current_size < target_size) {
            auto padding_frame = std::make_shared<PaddingFrame>();
            padding_frame->SetPaddingLength(target_size - current_size);
            if (!ctx.frame_visitor->HandleFrame(padding_frame)) {
                LOG_WARN("PacketBuilder::HandleInitialPacketRequirements: failed to add padding frame");
            } else {
                LOG_DEBUG(
                    "PacketBuilder::HandleInitialPacketRequirements: added %u bytes padding to reach %u bytes",
                    target_size - current_size, target_size);
            }
        }
    }
}

// ==================== High-level Interfaces Implementation ====================

PacketBuilder::BuildResult PacketBuilder::BuildDataPacket(const DataPacketContext& ctx,
    const std::shared_ptr<common::IBuffer>& output_buffer, PacketNumber& packet_number, SendControl& send_control) {
    BuildResult result;
    result.success = false;

    // 1. Validate required parameters
    if (!ctx.cryptographer) {
        result.error_message = "cryptographer is null";
        LOG_ERROR("PacketBuilder::BuildDataPacket: %s", result.error_message.c_str());
        return result;
    }

    if (!ctx.local_cid_manager || !ctx.remote_cid_manager) {
        result.error_message = "connection ID managers are null";
        LOG_ERROR("PacketBuilder::BuildDataPacket: %s", result.error_message.c_str());
        return result;
    }

    // 2. Create frame visitor with MTU limit
    //
    // Per-datagram payload budget for the frame visitor.
    //   kMaxV4PacketSize (1472)
    //     - long-header overhead (~13 B: 1 flag + 8 DCID + 4 PN max)
    //     - AEAD tag (16 B)
    //   = 1443 B available for plaintext frames.
    // We use 1420 to leave headroom for variable-length fields (token,
    // multi-byte CIDs, longer PN encoding) and to stay safely below the
    // typical Ethernet MTU once IP+UDP headers are added (28 B), so a
    // single packet never IP-fragments on the loopback / common LAN path.
    constexpr uint32_t kVisitorBudget = 1420;
    FixBufferFrameVisitor visitor(kVisitorBudget);

    // Set stream data size limit for flow control
    visitor.SetStreamDataSizeLimit(ctx.max_stream_data_size);

    // 3. Add all control frames
    for (auto& frame : ctx.frames) {
        if (!visitor.HandleFrame(frame)) {
            // Check if it's insufficient space or real error
            if (visitor.GetLastError() == FrameEncodeError::kInsufficientSpace) {
                LOG_DEBUG("PacketBuilder::BuildDataPacket: buffer full, stopping frame addition");
                break;  // Buffer full, but this is not an error
            }
            result.error_message = "failed to add frame type=" + std::to_string(frame->GetType());
            LOG_ERROR("PacketBuilder::BuildDataPacket: %s", result.error_message.c_str());
            return result;
        }
    }

    // 4. Add stream data (if requested and StreamManager provided)
    if (ctx.include_stream_data && ctx.stream_manager) {
        bool has_more = ctx.stream_manager->BuildStreamFrames(&visitor, ctx.level);
        LOG_DEBUG("PacketBuilder::BuildDataPacket: stream frames built, has_more=%d", has_more);
    }

    // 5. Check if we have any data
    auto payload_buffer = visitor.GetBuffer();
    if (!payload_buffer || payload_buffer->GetDataLength() == 0) {
        result.error_message = "no data to send";
        LOG_DEBUG("PacketBuilder::BuildDataPacket: %s", result.error_message.c_str());
        return result;  // Not an error, just no data
    }

    // 6. Handle Initial packet padding BEFORE creating the packet
    // This ensures the padding is included in the payload
    if (ctx.add_padding && ctx.level == kInitial) {
        uint32_t current_size = payload_buffer->GetDataLength();
        if (current_size < ctx.min_size) {
            auto padding_frame = std::make_shared<PaddingFrame>();
            padding_frame->SetPaddingLength(ctx.min_size - current_size);
            if (!visitor.HandleFrame(padding_frame)) {
                LOG_WARN("PacketBuilder::BuildDataPacket: failed to add padding frame");
            } else {
                LOG_DEBUG("PacketBuilder::BuildDataPacket: added %u bytes padding to reach %u bytes",
                    ctx.min_size - current_size, ctx.min_size);
            }
        }
    }

    // 6b. RFC 9001 §5.4.2 minimum-payload guarantee for protected packets.
    //
    // Header Protection draws a 16-byte sample starting 4 bytes after the
    // first byte of the Packet Number field. For that sample to lie
    // entirely inside the encrypted region, the encrypted payload must
    // be at least 4 + 16 = 20 bytes long. The encrypted payload is laid
    // out as:
    //     [ Packet Number (1..4 B) | Plaintext frames | AEAD tag (16 B) ]
    // The smallest case is PN_len = 1, which gives a hard lower bound on
    // the plaintext frames region of (20 - 1 - 16) = 3 bytes. We pad to
    // 4 bytes so the rule holds regardless of which PN_len the sender
    // ultimately picks (PN_len = 2 would still satisfy 4 + 2 + 16 = 22
    // >= 20 -- the +1 byte over the strict 3-byte floor is intentional
    // headroom, NOT a stricter RFC requirement).
    //
    // This matters in practice for *tiny* 1-RTT packets such as a
    // standalone PING (1 byte plaintext) emitted as a PTO probe (see
    // BaseConnection::SetApplicationProbeCallback). Without this padding
    // the receiver's Rtt1Packet::DecodeWithCrypto rejects the datagram
    // with "payload too short for header protection sample"; the sender
    // then keeps re-arming PTO and firing more probes that all get
    // dropped, until the idle timeout closes the connection.
    //
    // We deliberately apply this at *all* protected levels (Initial,
    // 0-RTT, Handshake, 1-RTT): the HP-sample requirement is identical
    // for each. For Initial, the much larger anti-amplification padding
    // in step 6 above (target 1200 B, RFC 9000 §14.1) makes this a
    // no-op, so guarding by level is unnecessary.
    {
        constexpr uint32_t kMinProtectedPlaintext = 4;
        uint32_t current_size = payload_buffer->GetDataLength();
        if (current_size < kMinProtectedPlaintext) {
            auto padding_frame = std::make_shared<PaddingFrame>();
            padding_frame->SetPaddingLength(kMinProtectedPlaintext - current_size);
            if (!visitor.HandleFrame(padding_frame)) {
                LOG_WARN("PacketBuilder::BuildDataPacket: failed to add HP-sample padding frame");
            } else {
                LOG_DEBUG("PacketBuilder::BuildDataPacket: added %u bytes HP-sample padding "
                          "(level=%u, plaintext was %u, now %u)",
                    kMinProtectedPlaintext - current_size, ctx.level,
                    current_size, kMinProtectedPlaintext);
            }
        }
    }

    // 7. Create packet object
    auto packet = CreatePacketByLevel(ctx.level);
    if (!packet) {
        result.error_message = "failed to create packet for level=" + std::to_string(ctx.level);
        LOG_ERROR("PacketBuilder::BuildDataPacket: %s", result.error_message.c_str());
        return result;
    }

    // 8. Set token for Initial packets
    if (ctx.level == kInitial && !ctx.token.empty()) {
        auto init_packet = std::static_pointer_cast<InitPacket>(packet);
        init_packet->SetToken((uint8_t*)ctx.token.data(), ctx.token.length());
        LOG_DEBUG("PacketBuilder::BuildDataPacket: set token of length %zu", ctx.token.length());
    }

    // 9. Set connection IDs
    SetConnectionIDs(packet, ctx.local_cid_manager, ctx.remote_cid_manager);

    // 10. Set version for long headers, key_phase for short headers
    auto header = packet->GetHeader();
    if (header->GetHeaderType() == PacketHeaderType::kLongHeader) {
        uint32_t version = ctx.quic_version != 0 ? ctx.quic_version : kQuicVersions[0];
        ((LongHeader*)header)->SetVersion(version);
    } else if (header->GetHeaderType() == PacketHeaderType::kShortHeader) {
        // RFC 9001 §6: Set Key Phase bit for 1-RTT packets
        header->GetShortHeaderFlag().SetKeyPhase(ctx.key_phase);
    }

    // 11. Assign packet number
    PacketNumberSpace ns = CryptoLevel2PacketNumberSpace(ctx.level);
    uint64_t pn = packet_number.NextPacketNumber(ns);
    packet->SetPacketNumber(pn);
    header->SetPacketNumberLength(PacketNumber::GetPacketNumberLength(pn));
    LOG_DEBUG("PacketBuilder::BuildDataPacket: assigned packet number %llu", pn);

    // 12. Set payload and cryptographer
    packet->SetPayload(payload_buffer->GetSharedReadableSpan());
    packet->SetCryptographer(ctx.cryptographer);

    // 13. Set frame type bit for ACK-eliciting detection
    packet->AddFrameTypeBit(static_cast<FrameTypeBit>(visitor.GetFrameTypeBit()));

    // 14. Encode packet to output buffer
    if (!packet->Encode(output_buffer)) {
        result.error_message = "failed to encode packet";
        LOG_ERROR("PacketBuilder::BuildDataPacket: %s", result.error_message.c_str());
        return result;
    }

    uint32_t encoded_size = output_buffer->GetDataLength();
    LOG_DEBUG("PacketBuilder::BuildDataPacket: encoded packet size=%u bytes", encoded_size);

    // 15. Record packet send event (for congestion control)
    auto stream_data_info = visitor.GetStreamDataInfo();
    send_control.OnPacketSend(common::UTCTimeMsec(), packet, encoded_size, stream_data_info);

    // 16. Success! Fill in result
    result.success = true;
    result.packet = packet;
    result.packet_number = pn;
    result.packet_size = encoded_size;
    result.stream_data_size = static_cast<uint32_t>(visitor.GetStreamDataSize());

    LOG_DEBUG("PacketBuilder::BuildDataPacket: successfully built packet at level=%d, pn=%llu, size=%u",
        ctx.level, pn, encoded_size);
    return result;
}

PacketBuilder::BuildResult PacketBuilder::BuildAckPacket(EncryptionLevel level,
    const std::shared_ptr<ICryptographer>& cryptographer, const std::shared_ptr<IFrame>& ack_frame,
    ConnectionIDManager* local_cid_mgr, ConnectionIDManager* remote_cid_mgr,
    const std::shared_ptr<common::IBuffer>& output_buffer, PacketNumber& packet_number, SendControl& send_control,
    uint32_t quic_version, uint8_t key_phase) {
    // Simplified: use DataPacketContext with only ACK frame
    DataPacketContext ctx;
    ctx.level = level;
    ctx.cryptographer = cryptographer;
    ctx.local_cid_manager = local_cid_mgr;
    ctx.remote_cid_manager = remote_cid_mgr;
    ctx.quic_version = quic_version;
    ctx.key_phase = key_phase;
    ctx.frames.push_back(ack_frame);
    ctx.include_stream_data = false;        // ACK packets don't include stream data
    ctx.add_padding = (level == kInitial);  // Initial packets need padding
    ctx.min_size = 1200;

    LOG_DEBUG("PacketBuilder::BuildAckPacket: building ACK packet at level=%d", level);
    return BuildDataPacket(ctx, output_buffer, packet_number, send_control);
}

PacketBuilder::BuildResult PacketBuilder::BuildImmediatePacket(const std::shared_ptr<IFrame>& frame,
    EncryptionLevel level, const std::shared_ptr<ICryptographer>& cryptographer, ConnectionIDManager* local_cid_mgr,
    ConnectionIDManager* remote_cid_mgr, const std::shared_ptr<common::IBuffer>& output_buffer,
    PacketNumber& packet_number, SendControl& send_control, uint32_t quic_version, uint8_t key_phase) {
    // Simplified: use DataPacketContext with single frame
    DataPacketContext ctx;
    ctx.level = level;
    ctx.cryptographer = cryptographer;
    ctx.local_cid_manager = local_cid_mgr;
    ctx.remote_cid_manager = remote_cid_mgr;
    ctx.quic_version = quic_version;
    ctx.key_phase = key_phase;
    ctx.frames.push_back(frame);
    ctx.include_stream_data = false;  // Immediate packets don't include stream data
    ctx.add_padding = false;          // No padding for immediate packets (except Initial)

    LOG_DEBUG("PacketBuilder::BuildImmediatePacket: building immediate packet with frame type=%d at level=%d",
        frame->GetType(), level);
    return BuildDataPacket(ctx, output_buffer, packet_number, send_control);
}

}  // namespace quic
}  // namespace quicx
