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
        common::LOG_ERROR("PacketBuilder::BuildPacket: %s", result.error_message.c_str());
        return result;
    }

    if (!ctx.frame_visitor) {
        result.error_message = "frame_visitor is null";
        common::LOG_ERROR("PacketBuilder::BuildPacket: %s", result.error_message.c_str());
        return result;
    }

    if (!ctx.local_cid_manager || !ctx.remote_cid_manager) {
        result.error_message = "connection ID managers are null";
        common::LOG_ERROR("PacketBuilder::BuildPacket: %s", result.error_message.c_str());
        return result;
    }

    // Create packet based on encryption level
    auto packet = CreatePacketByLevel(ctx.encryption_level);
    if (!packet) {
        result.error_message = "failed to create packet for encryption level";
        common::LOG_ERROR("PacketBuilder::BuildPacket: %s %d", result.error_message.c_str(), ctx.encryption_level);
        return result;
    }

    // Handle Initial packet requirements (token and padding)
    if (ctx.encryption_level == kInitial) {
        HandleInitialPacketRequirements(packet, ctx);
    }

    // Set connection IDs (source for long headers, destination for all)
    SetConnectionIDs(packet, ctx.local_cid_manager, ctx.remote_cid_manager);

    // Set version for long headers
    auto header = packet->GetHeader();
    if (header->GetHeaderType() == PacketHeaderType::kLongHeader) {
        ((LongHeader*)header)->SetVersion(kQuicVersions[0]);
    }

    // Set payload from frame visitor
    packet->SetPayload(ctx.frame_visitor->GetBuffer()->GetSharedReadableSpan());

    // Set cryptographer
    packet->SetCryptographer(ctx.cryptographer);

    // If packet number is provided, set it now
    if (ctx.packet_number != 0) {
        packet->SetPacketNumber(ctx.packet_number);
        header->SetPacketNumberLength(PacketNumber::GetPacketNumberLength(ctx.packet_number));
        common::LOG_DEBUG(
            "PacketBuilder::BuildPacket: set packet number %llu, length=%u", ctx.packet_number, header->GetPacketNumberLength());
    }

    result.success = true;
    result.packet = packet;
    common::LOG_DEBUG("PacketBuilder::BuildPacket: successfully built packet at level %d", ctx.encryption_level);
    return result;
}

std::shared_ptr<IPacket> PacketBuilder::CreatePacketByLevel(EncryptionLevel level) {
    switch (level) {
        case kInitial: {
            auto packet = std::make_shared<InitPacket>();
            common::LOG_DEBUG("PacketBuilder::CreatePacketByLevel: created InitPacket");
            return packet;
        }
        case kHandshake: {
            auto packet = std::make_shared<HandshakePacket>();
            common::LOG_DEBUG("PacketBuilder::CreatePacketByLevel: created HandshakePacket");
            return packet;
        }
        case kEarlyData: {
            auto packet = std::make_shared<Rtt0Packet>();
            common::LOG_DEBUG("PacketBuilder::CreatePacketByLevel: created Rtt0Packet");
            return packet;
        }
        case kApplication: {
            auto packet = std::make_shared<Rtt1Packet>();
            common::LOG_DEBUG("PacketBuilder::CreatePacketByLevel: created Rtt1Packet");
            return packet;
        }
        default:
            common::LOG_ERROR("PacketBuilder::CreatePacketByLevel: invalid encryption level %d", level);
            return nullptr;
    }
}

void PacketBuilder::SetConnectionIDs(
    std::shared_ptr<IPacket> packet, ConnectionIDManager* local_cid_manager, ConnectionIDManager* remote_cid_manager) {
    auto header = packet->GetHeader();

    // Set source CID for long headers
    if (header->GetHeaderType() == PacketHeaderType::kLongHeader) {
        auto local_cid = local_cid_manager->GetCurrentID();
        ((LongHeader*)header)->SetSourceConnectionId(local_cid.GetID(), local_cid.GetLength());
        common::LOG_DEBUG(
            "PacketBuilder::SetConnectionIDs: set source CID, length=%u, hash=%llu", local_cid.GetLength(), local_cid.Hash());
    }

    // Set destination CID for all packets
    auto remote_cid = remote_cid_manager->GetCurrentID();
    header->SetDestinationConnectionId(remote_cid.GetID(), remote_cid.GetLength());

    // Debug logging
    char dcid_hex[65] = {0};
    for (int i = 0; i < remote_cid.GetLength() && i < 32; i++) {
        sprintf(dcid_hex + i * 2, "%02x", remote_cid.GetID()[i]);
    }
    common::LOG_DEBUG("PacketBuilder::SetConnectionIDs: set destination CID, length=%u, hex=%s, hash=%llu",
        remote_cid.GetLength(), dcid_hex, remote_cid.Hash());
}

void PacketBuilder::HandleInitialPacketRequirements(std::shared_ptr<IPacket> packet, const BuildContext& ctx) {
    auto init_packet = std::dynamic_pointer_cast<InitPacket>(packet);
    if (!init_packet) {
        common::LOG_ERROR("PacketBuilder::HandleInitialPacketRequirements: packet is not InitPacket");
        return;
    }

    // Set token if provided
    if (ctx.token_data && ctx.token_length > 0) {
        init_packet->SetToken(const_cast<uint8_t*>(ctx.token_data), ctx.token_length);
        common::LOG_DEBUG(
            "PacketBuilder::HandleInitialPacketRequirements: set token of length %zu", ctx.token_length);
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
                common::LOG_WARN("PacketBuilder::HandleInitialPacketRequirements: failed to add padding frame");
            } else {
                common::LOG_DEBUG("PacketBuilder::HandleInitialPacketRequirements: added %u bytes padding to reach %u bytes",
                    target_size - current_size, target_size);
            }
        }
    }
}

// ==================== High-level Interfaces Implementation ====================

PacketBuilder::BuildResult PacketBuilder::BuildDataPacket(
    const DataPacketContext& ctx,
    std::shared_ptr<common::IBuffer> output_buffer,
    PacketNumber& packet_number,
    SendControl& send_control) {

    BuildResult result;
    result.success = false;

    // 1. Validate required parameters
    if (!ctx.cryptographer) {
        result.error_message = "cryptographer is null";
        common::LOG_ERROR("PacketBuilder::BuildDataPacket: %s", result.error_message.c_str());
        return result;
    }

    if (!ctx.local_cid_manager || !ctx.remote_cid_manager) {
        result.error_message = "connection ID managers are null";
        common::LOG_ERROR("PacketBuilder::BuildDataPacket: %s", result.error_message.c_str());
        return result;
    }

    // 2. Create frame visitor with reasonable MTU limit (1450 bytes)
    FixBufferFrameVisitor visitor(1450);

    // 3. Add all control frames
    for (auto& frame : ctx.frames) {
        if (!visitor.HandleFrame(frame)) {
            // Check if it's insufficient space or real error
            if (visitor.GetLastError() == FrameEncodeError::kInsufficientSpace) {
                common::LOG_DEBUG("PacketBuilder::BuildDataPacket: buffer full, stopping frame addition");
                break;  // Buffer full, but this is not an error
            }
            result.error_message = "failed to add frame type=" + std::to_string(frame->GetType());
            common::LOG_ERROR("PacketBuilder::BuildDataPacket: %s", result.error_message.c_str());
            return result;
        }
    }

    // 4. Add stream data (if requested and StreamManager provided)
    if (ctx.include_stream_data && ctx.stream_manager) {
        bool has_more = ctx.stream_manager->BuildStreamFrames(&visitor, ctx.level);
        common::LOG_DEBUG("PacketBuilder::BuildDataPacket: stream frames built, has_more=%d", has_more);
    }

    // 5. Check if we have any data
    auto payload_buffer = visitor.GetBuffer();
    if (!payload_buffer || payload_buffer->GetDataLength() == 0) {
        result.error_message = "no data to send";
        common::LOG_DEBUG("PacketBuilder::BuildDataPacket: %s", result.error_message.c_str());
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
                common::LOG_WARN("PacketBuilder::BuildDataPacket: failed to add padding frame");
            } else {
                common::LOG_DEBUG("PacketBuilder::BuildDataPacket: added %u bytes padding to reach %u bytes",
                    ctx.min_size - current_size, ctx.min_size);
            }
        }
    }

    // 7. Create packet object
    auto packet = CreatePacketByLevel(ctx.level);
    if (!packet) {
        result.error_message = "failed to create packet for level=" + std::to_string(ctx.level);
        common::LOG_ERROR("PacketBuilder::BuildDataPacket: %s", result.error_message.c_str());
        return result;
    }

    // 8. Set token for Initial packets
    if (ctx.level == kInitial && !ctx.token.empty()) {
        auto init_packet = std::static_pointer_cast<InitPacket>(packet);
        init_packet->SetToken((uint8_t*)ctx.token.data(), ctx.token.length());
        common::LOG_DEBUG("PacketBuilder::BuildDataPacket: set token of length %zu", ctx.token.length());
    }

    // 9. Set connection IDs
    SetConnectionIDs(packet, ctx.local_cid_manager, ctx.remote_cid_manager);

    // 10. Set version for long headers
    auto header = packet->GetHeader();
    if (header->GetHeaderType() == PacketHeaderType::kLongHeader) {
        ((LongHeader*)header)->SetVersion(kQuicVersions[0]);
    }

    // 11. Assign packet number
    PacketNumberSpace ns = CryptoLevel2PacketNumberSpace(ctx.level);
    uint64_t pn = packet_number.NextPakcetNumber(ns);
    packet->SetPacketNumber(pn);
    header->SetPacketNumberLength(PacketNumber::GetPacketNumberLength(pn));
    common::LOG_DEBUG("PacketBuilder::BuildDataPacket: assigned packet number %llu", pn);

    // 12. Set payload and cryptographer
    packet->SetPayload(payload_buffer->GetSharedReadableSpan());
    packet->SetCryptographer(ctx.cryptographer);

    // 13. Encode packet to output buffer
    if (!packet->Encode(output_buffer)) {
        result.error_message = "failed to encode packet";
        common::LOG_ERROR("PacketBuilder::BuildDataPacket: %s", result.error_message.c_str());
        return result;
    }

    uint32_t encoded_size = output_buffer->GetDataLength();
    common::LOG_DEBUG("PacketBuilder::BuildDataPacket: encoded packet size=%u bytes", encoded_size);

    // 14. Record packet send event (for congestion control)
    auto stream_data_info = visitor.GetStreamDataInfo();
    send_control.OnPacketSend(common::UTCTimeMsec(), packet, encoded_size, stream_data_info);

    // 15. Success! Fill in result
    result.success = true;
    result.packet = packet;
    result.packet_number = pn;
    result.packet_size = encoded_size;

    common::LOG_DEBUG("PacketBuilder::BuildDataPacket: successfully built packet at level=%d, pn=%llu, size=%u",
        ctx.level, pn, encoded_size);
    return result;
}

PacketBuilder::BuildResult PacketBuilder::BuildAckPacket(
    EncryptionLevel level,
    std::shared_ptr<ICryptographer> cryptographer,
    std::shared_ptr<IFrame> ack_frame,
    ConnectionIDManager* local_cid_mgr,
    ConnectionIDManager* remote_cid_mgr,
    std::shared_ptr<common::IBuffer> output_buffer,
    PacketNumber& packet_number,
    SendControl& send_control) {

    // Simplified: use DataPacketContext with only ACK frame
    DataPacketContext ctx;
    ctx.level = level;
    ctx.cryptographer = cryptographer;
    ctx.local_cid_manager = local_cid_mgr;
    ctx.remote_cid_manager = remote_cid_mgr;
    ctx.frames.push_back(ack_frame);
    ctx.include_stream_data = false;  // ACK packets don't include stream data
    ctx.add_padding = (level == kInitial);  // Initial packets need padding
    ctx.min_size = 1200;

    common::LOG_DEBUG("PacketBuilder::BuildAckPacket: building ACK packet at level=%d", level);
    return BuildDataPacket(ctx, output_buffer, packet_number, send_control);
}

PacketBuilder::BuildResult PacketBuilder::BuildImmediatePacket(
    std::shared_ptr<IFrame> frame,
    EncryptionLevel level,
    std::shared_ptr<ICryptographer> cryptographer,
    ConnectionIDManager* local_cid_mgr,
    ConnectionIDManager* remote_cid_mgr,
    std::shared_ptr<common::IBuffer> output_buffer,
    PacketNumber& packet_number,
    SendControl& send_control) {

    // Simplified: use DataPacketContext with single frame
    DataPacketContext ctx;
    ctx.level = level;
    ctx.cryptographer = cryptographer;
    ctx.local_cid_manager = local_cid_mgr;
    ctx.remote_cid_manager = remote_cid_mgr;
    ctx.frames.push_back(frame);
    ctx.include_stream_data = false;  // Immediate packets don't include stream data
    ctx.add_padding = false;  // No padding for immediate packets (except Initial)

    common::LOG_DEBUG("PacketBuilder::BuildImmediatePacket: building immediate packet with frame type=%d at level=%d",
        frame->GetType(), level);
    return BuildDataPacket(ctx, output_buffer, packet_number, send_control);
}

}  // namespace quic
}  // namespace quicx
