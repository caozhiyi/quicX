#include "quic/connection/packet_builder.h"

#include <algorithm>
#include <cstdio>

#include "common/buffer/single_block_buffer.h"
#include "common/decode/decode.h"
#include "common/log/log.h"
#include "common/util/time.h"
#include "quicx/common/metrics.h"
#include "quicx/common/metrics_std.h"

#include "quic/common/constants.h"
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

namespace {

constexpr uint32_t kAeadTagLength = 16;
// Narrowest packet-number encoding the builder may choose.
constexpr uint32_t kMinPacketNumberLength = 1;

// Estimate the number of bytes this packet's header occupies ahead of the
// encrypted payload.
//
// `pn_length` is the packet-number encoding to assume. When the caller must
// clear a lower bound such as the 1200 B floor of RFC 9000 §14.1, pass the
// narrowest (kMinPacketNumberLength): under-estimating the header only makes
// us pad *more*, which is safe, whereas over-estimating it would shrink the
// padding and let the datagram fall below the floor, where the peer silently
// drops it.
uint32_t EstimateHeaderOverhead(const PacketBuilder::DataPacketContext& ctx, uint32_t pn_length) {
    uint32_t dcid_length = 0;
    if (ctx.remote_cid_manager) {
        dcid_length = ctx.remote_cid_manager->GetCurrentID().GetLength();
    }
    // 1-RTT short header: 1 B flags + DCID (not length-prefixed; the peer
    // already knows its own CID length) + packet number.
    if (ctx.level == kApplication) {
        return 1 + dcid_length + pn_length;
    }

    uint32_t scid_length = 0;
    if (ctx.local_cid_manager) {
        scid_length = ctx.local_cid_manager->GetCurrentID().GetLength();
    }
    // Long header: 1 B flags + 4 B version + DCID (1 B len + id) + SCID
    // (1 B len + id) + 2 B Length varint (payloads here always exceed the
    // 63 B single-byte range) + packet number.
    uint32_t overhead = 1 + 4 + 1 + dcid_length + 1 + scid_length + 2 + pn_length;
    // Only Initial packets carry a token, prefixed by its own varint length.
    // Post-Retry this dominates the header (e.g. aioquic issues 256 B tokens).
    if (ctx.level == kInitial) {
        const uint64_t token_length = ctx.token.length();
        overhead += common::GetEncodeVarintLength(token_length) + static_cast<uint32_t>(token_length);
    }
    return overhead;
}

// Append a PADDING frame so the encoded datagram reaches the RFC 9000 §14.1
// on-wire floor `ctx.min_size`.
//
// The header estimate, the AEAD tag, and any bytes already in the datagram
// (`pre_size`, for coalesced packets) form the non-payload part; the rest must
// be filled by plaintext frames. The PADDING frame itself costs one type byte
// and is bounded by the payload buffer's `visitor_budget`, so we add only as
// much padding as actually fits. One extra byte of padding is intentionally
// kept as headroom against header-size estimation error. When the floor is
// unreachable we pad to the buffer limit rather than overflowing it.
void PadToMinSize(const PacketBuilder::DataPacketContext& ctx, FixBufferFrameVisitor& visitor,
                  uint32_t current_size, uint32_t visitor_budget, uint32_t pre_size) {
    const uint32_t header_overhead = EstimateHeaderOverhead(ctx, kMinPacketNumberLength);
    const uint32_t non_payload = pre_size + header_overhead + kAeadTagLength;
    // Plaintext bytes still needed so the datagram reaches the floor (the +1
    // headroom lives in the resulting padding below).
    const int64_t needed = static_cast<int64_t>(ctx.min_size) - static_cast<int64_t>(non_payload) -
                           static_cast<int64_t>(current_size);
    if (needed <= 0) {
        return;  // Already at or above the floor.
    }
    // Only (visitor_budget - current_size - 1) bytes of padding content fit
    // once the PADDING type byte is reserved.
    const uint32_t room = visitor_budget > current_size + 1 ? visitor_budget - current_size - 1 : 0;
    const uint32_t padding_length = std::min<uint32_t>(static_cast<uint32_t>(needed), room);
    if (padding_length == 0) {
        return;
    }
    auto padding_frame = std::make_shared<PaddingFrame>();
    padding_frame->SetPaddingLength(padding_length);
    if (!visitor.HandleFrame(padding_frame)) {
        LOG_WARN("PacketBuilder::BuildDataPacket: failed to add padding frame");
    } else {
        LOG_DEBUG("PacketBuilder::BuildDataPacket: added %u bytes padding to reach %u-byte floor (level=%d)",
            padding_length, ctx.min_size, ctx.level);
    }
}

}  // namespace

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
        LOG_DEBUG("PacketBuilder::SetConnectionIDs: set source CID, length=%u, hash=%llu", local_cid.GetLength(),
            local_cid.Hash());
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
    // RFC 9000 §14.1: Initial packets MUST be at least kMinInitialPacketSize
    // bytes (=1200) so anti-amplification budget covers PATH_CHALLENGE.
    if (ctx.add_padding) {
        uint32_t current_size = ctx.frame_visitor->GetBuffer()->GetDataLength();
        uint32_t target_size = kMinInitialPacketSize;

        if (current_size < target_size) {
            auto padding_frame = std::make_shared<PaddingFrame>();
            padding_frame->SetPaddingLength(target_size - current_size);
            if (!ctx.frame_visitor->HandleFrame(padding_frame)) {
                LOG_WARN("PacketBuilder::HandleInitialPacketRequirements: failed to add padding frame");
            } else {
                LOG_DEBUG("PacketBuilder::HandleInitialPacketRequirements: added %u bytes padding to reach %u bytes",
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

    // Phase-level timing probes. Cheap (~30ns/read on steady_clock) and the
    // HistogramObserve calls themselves short-circuit when the metric is
    // disabled, so leaving them in the hot path costs nothing in production
    // builds where diag_* metrics are off. See aliased breakdown below:
    //   t0 -> t1 : visitor construct + frame add + stream frames (build_phase_frames_us)
    //   t1 -> t2 : packet object create + header setup + payload bind   (build_phase_setup_us)
    //   t2 -> t3 : packet->Encode (AEAD seal + header protection)        (build_phase_encode_us)
    //   t3 -> t4 : send_control bookkeeping (OnPacketSend + result fill) (build_phase_record_us)
    const uint64_t t0 = common::Metrics::NowUs();

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

    // Snapshot the buffer's pre-existing data length so the final
    // `encoded_size` reflects only the bytes this BuildDataPacket call
    // appended. This matters for the Initial+Handshake coalescing path
    // where the caller hands us a buffer that *already* contains the
    // preceding QUIC packet (Initial), and we are appending the next one
    // (Handshake) to the same datagram. Without this snapshot,
    // GetDataLength() at the bottom would yield the combined datagram
    // length and SendControl::OnPacketSend would over-charge cwnd /
    // bytes-in-flight by the Initial's size, severely skewing congestion
    // control during the handshake.
    //
    // All existing (single-packet) callers pass a fresh empty buffer, so
    // pre_size = 0 there and the arithmetic is a no-op.
    const uint32_t pre_size = output_buffer ? output_buffer->GetDataLength() : 0;

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

    // The budget above describes a datagram that starts empty. On the
    // Initial+Handshake coalescing path the caller hands us a buffer that
    // already holds the preceding Initial packet, so the space actually
    // left for this packet is smaller by exactly `pre_size`. Failing to
    // subtract it lets the frame visitor fill up to the full 1420 B, the
    // encoded packet then runs past the end of the datagram buffer and
    // AEAD sealing fails with "EVP_AEAD_CTX_seal failed" (no room for the
    // ciphertext + 16 B tag), which stalls the handshake into PTO loops.
    // `pre_size` is 0 for every single-packet caller, so this is a no-op
    // outside the coalescing path.
    if (pre_size >= kVisitorBudget) {
        result.error_message = "no datagram space left for coalesced packet";
        LOG_WARN("PacketBuilder::BuildDataPacket: %s (pre_size=%u)", result.error_message.c_str(), pre_size);
        return result;
    }
    const uint32_t visitor_budget = kVisitorBudget - pre_size;
    FixBufferFrameVisitor visitor(visitor_budget);

    // Set stream data size limit for flow control. Never let the flow-control
    // allowance exceed the physical room remaining in this datagram.
    visitor.SetStreamDataSizeLimit(std::min<uint64_t>(ctx.max_stream_data_size, visitor_budget));

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

    // 6. Handle datagram-size padding BEFORE creating the packet
    // This ensures the padding is included in the payload.
    //
    // Historically this branch was gated by `ctx.level == kInitial` because
    // the only call site that asked for padding was the Initial-bringup
    // path (RFC 9000 §14.1: client MUST expand carrying-Initial datagrams
    // to >= 1200 B). The level gate has been removed so that the
    // coalesced-Initial+Handshake send path can request padding to be
    // carried in the *Handshake* packet within the same datagram, which
    // RFC 9000 §14.1 + §12.2 explicitly allow ("by coalescing the Initial
    // packet"). The 1200-byte rule applies to the on-wire datagram, not
    // to any specific packet within it, so it's legal — and cheaper at
    // the receiver — to let the trailing Handshake packet carry the
    // PADDING frames. All existing single-Initial callers still work
    // unchanged because they continue to pass add_padding=true with
    // min_size=kMinInitialPacketSize.
    if (ctx.add_padding) {
        PadToMinSize(ctx, visitor, payload_buffer->GetDataLength(), visitor_budget, pre_size);
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
                LOG_DEBUG(
                    "PacketBuilder::BuildDataPacket: added %u bytes HP-sample padding "
                    "(level=%u, plaintext was %u, now %u)",
                    kMinProtectedPlaintext - current_size, ctx.level, current_size, kMinProtectedPlaintext);
            }
        }
    }

    // 7. Create packet object
    const uint64_t t1 = common::Metrics::NowUs();
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

    // 12b. qlog draft-03: hand the visitor's encoded-frame list to the
    //      packet so SendControl::OnPacketSend can emit per-frame qlog
    //      data. Outbound IPackets normally don't store frame objects —
    //      they hold raw encoded bytes in `payload_` — so without this
    //      step packet_sent events would carry `"frames":[]`. The four
    //      packet types we may have constructed here (Initial / Handshake
    //      / 0-RTT / 1-RTT) all override GetFrames() to return their own
    //      mutable frames_list_, so assigning through the reference is
    //      safe. Retry / VersionNegotiation packets never reach this
    //      path.
    {
        auto& packet_frames = packet->GetFrames();
        packet_frames = visitor.TakeHandledFrames();
    }

    // 13. Set frame type bit for ACK-eliciting detection
    packet->AddFrameTypeBit(static_cast<FrameTypeBit>(visitor.GetFrameTypeBit()));


    // 14. Encode packet to output buffer
    const uint64_t t2 = common::Metrics::NowUs();
    if (!packet->Encode(output_buffer)) {
        result.error_message = "failed to encode packet";
        LOG_ERROR("PacketBuilder::BuildDataPacket: %s", result.error_message.c_str());
        return result;
    }
    const uint64_t t3 = common::Metrics::NowUs();

    uint32_t encoded_size = output_buffer->GetDataLength() - pre_size;
    LOG_DEBUG("PacketBuilder::BuildDataPacket: encoded packet size=%u bytes (pre=%u, total=%u)", encoded_size, pre_size,
        output_buffer->GetDataLength());

    // 15. Record packet send event (for congestion control)
    auto stream_data_info = visitor.GetStreamDataInfo();
    send_control.OnPacketSend(common::UTCTimeMsec(), packet, encoded_size, stream_data_info);

    // 16. Success! Fill in result
    result.success = true;
    result.packet = packet;
    result.packet_number = pn;
    result.packet_size = encoded_size;
    result.stream_data_size = static_cast<uint32_t>(visitor.GetStreamDataSize());

    LOG_DEBUG("PacketBuilder::BuildDataPacket: successfully built packet at level=%d, pn=%llu, size=%u", ctx.level, pn,
        encoded_size);

    // Emit phase breakdown. All four observations happen together so the
    // sample counts stay consistent across phases (a missing observation in
    // one phase would make ratio reasoning unreliable). NowUs() is monotonic
    // steady_clock so we don't have to guard against backward jumps.
    const uint64_t t4 = common::Metrics::NowUs();
    common::Metrics::HistogramObserve(common::MetricsStd::DiagBuildPhaseFramesUs, t1 - t0);
    common::Metrics::HistogramObserve(common::MetricsStd::DiagBuildPhaseSetupUs, t2 - t1);
    common::Metrics::HistogramObserve(common::MetricsStd::DiagBuildPhaseEncodeUs, t3 - t2);
    common::Metrics::HistogramObserve(common::MetricsStd::DiagBuildPhaseRecordUs, t4 - t3);

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
    ctx.min_size = kMinInitialPacketSize;   // RFC 9000 §14.1

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
