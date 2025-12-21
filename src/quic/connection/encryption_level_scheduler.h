#ifndef QUIC_CONNECTION_ENCRYPTION_LEVEL_SCHEDULER
#define QUIC_CONNECTION_ENCRYPTION_LEVEL_SCHEDULER

#include <cstdint>

#include "quic/crypto/tls/type.h"
#include "quic/packet/type.h"

namespace quicx {
namespace quic {

// Forward declarations
class ConnectionCrypto;
class RecvControl;
class PathManager;

/**
 * @brief Encryption level scheduler for unified encryption level management
 *
 * This component centralizes all encryption level selection logic that was previously
 * scattered across BaseConnection::GetCurEncryptionLevel() and GenerateSendData().
 *
 * Key responsibilities:
 * 1. Determine the next encryption level for sending packets
 * 2. Handle cross-level ACK priorities (send ACKs at correct encryption level)
 * 3. Manage 0-RTT early data scheduling (must send Initial first, then 0-RTT)
 * 4. Handle path validation requirements (PATH_CHALLENGE/RESPONSE need Application level)
 *
 * Priority order (highest to lowest):
 * 1. Cross-level pending ACKs (most critical - affects peer's congestion control)
 * 2. Path validation (PATH_CHALLENGE/RESPONSE at Application level)
 * 3. 0-RTT early data (if conditions met)
 * 4. Current encryption level (normal flow)
 *
 * Usage:
 *   auto ctx = scheduler.GetNextSendContext();
 *   auto cryptographer = crypto.GetCryptographer(ctx.level);
 *   if (ctx.has_pending_ack) {
 *       // Send ACK frame for ctx.ack_space
 *   }
 *   // Build and send packet at ctx.level
 */
class EncryptionLevelScheduler {
public:
    /**
     * @brief Send context returned by GetNextSendContext()
     */
    struct SendContext {
        EncryptionLevel level;            // Encryption level to use for next packet
        bool has_pending_ack;             // Whether there's a pending ACK to send
        PacketNumberSpace ack_space;      // Packet number space for the ACK (if has_pending_ack)
        bool is_path_probe;               // Whether this is for path validation

        SendContext()
            : level(kInitial), has_pending_ack(false), ack_space(kInitialNumberSpace), is_path_probe(false) {}
    };

    /**
     * @brief Constructor
     *
     * @param crypto Connection crypto module (for checking available cryptographers)
     * @param recv_control Receive control module (for checking pending ACKs)
     * @param path_manager Path manager (for checking path validation state)
     */
    EncryptionLevelScheduler(ConnectionCrypto& crypto, RecvControl& recv_control, PathManager& path_manager);

    ~EncryptionLevelScheduler() = default;

    /**
     * @brief Get the send context for the next packet
     *
     * This is the main API. Call this to determine what encryption level to use
     * and whether there are any special requirements (ACK, path probe, etc.)
     *
     * @return SendContext with encryption level and metadata
     */
    SendContext GetNextSendContext();

    /**
     * @brief Mark that there is early data (0-RTT) pending to send
     *
     * This affects the scheduling logic: if early data is pending and 0-RTT keys
     * are available, the scheduler may return kEarlyData level.
     *
     * @param pending true if there's application data waiting to send
     */
    void SetEarlyDataPending(bool pending) { has_early_data_pending_ = pending; }

    /**
     * @brief Mark that the Initial packet has been sent
     *
     * Required for 0-RTT: must send Initial packet (with ClientHello) before
     * sending 0-RTT packets.
     *
     * @param sent true after Initial packet is sent
     */
    void SetInitialPacketSent(bool sent = true) { initial_packet_sent_ = sent; }

    /**
     * @brief Check if Initial packet has been sent
     *
     * @return true if Initial packet was sent
     */
    bool IsInitialPacketSent() const { return initial_packet_sent_; }

private:
    /**
     * @brief Check if there are cross-level pending ACKs
     *
     * Cross-level ACKs occur when:
     * - We're at Handshake/Application level but have pending Initial ACKs
     * - We're at Application level but have pending Handshake ACKs
     *
     * These must be sent immediately at the correct encryption level to avoid
     * delaying the peer's congestion control and packet loss detection.
     *
     * @param ctx Output parameter to fill if cross-level ACK found
     * @return true if there's a cross-level ACK that needs sending
     */
    bool HasCrossLevelPendingAck(SendContext& ctx);

    /**
     * @brief Try to get 0-RTT encryption level
     *
     * Returns true if 0-RTT conditions are met:
     * - Early data is pending
     * - Current level is Initial
     * - 0-RTT cryptographer is available
     * - Initial packet has been sent (ClientHello sent first)
     *
     * @param ctx Output parameter to fill if 0-RTT is available
     * @return true if should use 0-RTT level
     */
    bool TryGet0RttLevel(SendContext& ctx);

private:
    ConnectionCrypto& crypto_;
    RecvControl& recv_control_;
    PathManager& path_manager_;

    bool has_early_data_pending_;  // Whether there's application data waiting (0-RTT)
    bool initial_packet_sent_;     // Whether Initial packet was sent (for 0-RTT ordering)
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_ENCRYPTION_LEVEL_SCHEDULER
