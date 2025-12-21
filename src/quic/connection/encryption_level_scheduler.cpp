#include "quic/connection/encryption_level_scheduler.h"

#include "common/log/log.h"
#include "common/util/time.h"

#include "quic/connection/connection_crypto.h"
#include "quic/connection/connection_path_manager.h"
#include "quic/connection/controler/recv_control.h"
#include "quic/connection/util.h"
#include "quic/crypto/tls/type.h"

namespace quicx {
namespace quic {

EncryptionLevelScheduler::EncryptionLevelScheduler(
    ConnectionCrypto& crypto, RecvControl& recv_control, PathManager& path_manager)
    : crypto_(crypto),
      recv_control_(recv_control),
      path_manager_(path_manager),
      has_early_data_pending_(false),
      initial_packet_sent_(false) {}

EncryptionLevelScheduler::SendContext EncryptionLevelScheduler::GetNextSendContext() {
    SendContext ctx;

    // Priority 1: Cross-level pending ACKs (highest priority)
    // These ACKs must be sent at the correct encryption level to avoid delaying
    // the peer's loss detection and congestion control.
    if (HasCrossLevelPendingAck(ctx)) {
        common::LOG_INFO(
            "EncryptionLevelScheduler: Cross-level ACK pending for space %d at level %d", ctx.ack_space, ctx.level);
        return ctx;
    }

    // Priority 2: Path validation (needs Application level)
    // PATH_CHALLENGE and PATH_RESPONSE frames can only be sent in 1-RTT packets.
    // If we're probing a path and Application keys are ready, use them.
    if (path_manager_.IsPathProbeInflight()) {
        auto app_crypto = crypto_.GetCryptographer(kApplication);
        if (app_crypto) {
            ctx.level = kApplication;
            ctx.is_path_probe = true;
            ctx.has_pending_ack = false;
            common::LOG_DEBUG("EncryptionLevelScheduler: Path probe active, using Application level");
            return ctx;
        }
    }

    // Priority 3: 0-RTT early data (if conditions met)
    // Must send Initial packet first (with ClientHello), then can send 0-RTT.
    if (has_early_data_pending_ && TryGet0RttLevel(ctx)) {
        common::LOG_INFO("EncryptionLevelScheduler: Using 0-RTT level for early data");
        return ctx;
    }

    // Priority 4: Current encryption level (normal flow)
    ctx.level = crypto_.GetCurEncryptionLevel();
    ctx.has_pending_ack = false;
    ctx.is_path_probe = false;

    // Check if there's a pending ACK at the current level
    auto ns = CryptoLevel2PacketNumberSpace(ctx.level);
    auto ack = recv_control_.MayGenerateAckFrame(common::UTCTimeMsec(), ns, true);
    if (ack) {
        ctx.has_pending_ack = true;
        ctx.ack_space = ns;
        common::LOG_DEBUG("EncryptionLevelScheduler: Pending ACK at current level %d space %d", ctx.level, ns);
    }

    return ctx;
}

bool EncryptionLevelScheduler::HasCrossLevelPendingAck(SendContext& ctx) {
    auto current_level = crypto_.GetCurEncryptionLevel();

    // If we're at Handshake or Application level, check for pending Initial ACKs
    if (current_level >= kHandshake) {
        auto init_ack = recv_control_.MayGenerateAckFrame(common::UTCTimeMsec(), kInitialNumberSpace, true);
        if (init_ack) {
            // There's a pending Initial ACK, check if we have Initial keys
            auto init_crypto = crypto_.GetCryptographer(kInitial);
            if (init_crypto) {
                ctx.level = kInitial;
                ctx.has_pending_ack = true;
                ctx.ack_space = kInitialNumberSpace;
                ctx.is_path_probe = false;
                common::LOG_DEBUG("EncryptionLevelScheduler: Cross-level Initial ACK needed (current level=%d)",
                    current_level);
                return true;
            } else {
                // Keys discarded, cannot send ACK
                // This is expected after key update - the ACK will be dropped by RecvControl
                common::LOG_DEBUG(
                    "EncryptionLevelScheduler: Initial ACK pending but keys discarded (current level=%d)",
                    current_level);
            }
        }
    }

    // If we're at Application level, check for pending Handshake ACKs
    if (current_level >= kApplication) {
        auto hs_ack = recv_control_.MayGenerateAckFrame(common::UTCTimeMsec(), kHandshakeNumberSpace, true);
        if (hs_ack) {
            auto hs_crypto = crypto_.GetCryptographer(kHandshake);
            if (hs_crypto) {
                ctx.level = kHandshake;
                ctx.has_pending_ack = true;
                ctx.ack_space = kHandshakeNumberSpace;
                ctx.is_path_probe = false;
                common::LOG_DEBUG("EncryptionLevelScheduler: Cross-level Handshake ACK needed (current level=%d)",
                    current_level);
                return true;
            } else {
                common::LOG_DEBUG(
                    "EncryptionLevelScheduler: Handshake ACK pending but keys discarded (current level=%d)",
                    current_level);
            }
        }
    }

    return false;
}

bool EncryptionLevelScheduler::TryGet0RttLevel(SendContext& ctx) {
    auto current_level = crypto_.GetCurEncryptionLevel();

    // 0-RTT is only relevant when current level is Initial
    if (current_level != kInitial) {
        return false;
    }

    // Check if 0-RTT cryptographer is available
    auto early_data_crypto = crypto_.GetCryptographer(kEarlyData);
    if (!early_data_crypto) {
        return false;
    }

    // Must send Initial packet first (with ClientHello)
    // This ensures proper handshake ordering per RFC 9001
    if (!initial_packet_sent_) {
        common::LOG_DEBUG("EncryptionLevelScheduler: 0-RTT keys available but Initial packet not sent yet");
        return false;
    }

    // All conditions met, use 0-RTT
    ctx.level = kEarlyData;
    ctx.has_pending_ack = false;
    ctx.is_path_probe = false;
    return true;
}

}  // namespace quic
}  // namespace quicx
