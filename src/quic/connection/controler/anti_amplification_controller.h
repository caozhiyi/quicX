#ifndef QUIC_CONNECTION_CONTROLER_ANTI_AMPLIFICATION_CONTROLLER
#define QUIC_CONNECTION_CONTROLER_ANTI_AMPLIFICATION_CONTROLLER

#include <cstdint>

namespace quicx {
namespace quic {

/**
 * @brief Anti-amplification controller for unvalidated addresses
 *
 * RFC 9000 Section 8: Address Validation
 *
 * To prevent QUIC from being used in amplification attacks, servers MUST limit
 * the amount of data they send to unvalidated client addresses. Before address
 * validation completes, servers cannot send more than 3 times the amount of data
 * received from the client.
 *
 * Key responsibilities:
 * 1. Track bytes sent to and received from unvalidated addresses
 * 2. Enforce the 3x amplification limit
 * 3. Provide initial credit to allow sending PATH_CHALLENGE
 * 4. Allow unrestricted sending after address validation
 *
 * Usage pattern:
 * - Server starts in validated state (handshake validates initial address)
 * - When starting path validation to a new address, call EnterUnvalidatedState()
 * - Call OnBytesReceived() for every packet received from the candidate address
 * - Call CanSend() before sending each packet
 * - Call OnBytesSent() after successfully sending each packet
 * - Call ExitUnvalidatedState() when address is validated
 *
 * Thread safety: Not thread-safe. Caller must provide synchronization.
 */
class AntiAmplificationController {
public:
    /**
     * @brief Constructor - starts in validated state (no restrictions)
     */
    AntiAmplificationController();

    ~AntiAmplificationController() = default;

    /**
     * @brief Enter unvalidated state for a new address
     *
     * Resets counters and provides initial credit to allow sending
     * PATH_CHALLENGE even before receiving any data.
     *
     * @param initial_credit Initial bytes credit (default: 400 bytes, allows ~1200 bytes under 3x rule)
     */
    void EnterUnvalidatedState(uint64_t initial_credit = kDefaultInitialCredit);

    /**
     * @brief Exit unvalidated state (address validated)
     *
     * Removes all sending restrictions. Should be called when:
     * - PATH_RESPONSE is successfully received
     * - Handshake packet is successfully decrypted from this address
     * - 1-RTT packet ACK is received from this address
     */
    void ExitUnvalidatedState();

    /**
     * @brief Record bytes received from the unvalidated address
     *
     * Increases the send budget according to the 3x rule.
     *
     * @param bytes Number of bytes received
     */
    void OnBytesReceived(uint64_t bytes);

    /**
     * @brief Record bytes sent to the unvalidated address
     *
     * Must be called after successfully sending a packet. Updates the
     * sent bytes counter.
     *
     * @param bytes Number of bytes sent
     */
    void OnBytesSent(uint64_t bytes);

    /**
     * @brief Check if we can send a packet of given size
     *
     * Verifies if sending the packet would violate the 3x amplification limit.
     *
     * @param bytes Size of the packet to send
     * @return true if allowed to send, false if would exceed limit
     */
    bool CanSend(uint64_t bytes) const;

    /**
     * @brief Check if currently in unvalidated state
     *
     * @return true if address is unvalidated (restrictions active), false otherwise
     */
    bool IsUnvalidated() const { return is_unvalidated_; }

    /**
     * @brief Get current send budget remaining
     *
     * Returns how many more bytes can be sent before hitting the limit.
     *
     * @return Remaining bytes that can be sent (0 if budget exhausted or validated)
     */
    uint64_t GetRemainingBudget() const;

    /**
     * @brief Get total bytes sent to unvalidated address
     *
     * @return Total bytes sent
     */
    uint64_t GetBytesSent() const { return sent_bytes_; }

    /**
     * @brief Get total bytes received from unvalidated address
     *
     * @return Total bytes received
     */
    uint64_t GetBytesReceived() const { return received_bytes_; }

    /**
     * @brief Check if approaching amplification limit (for Retry consideration)
     *
     * Server may want to send a Retry packet if it's approaching the amplification
     * limit but hasn't validated the address yet.
     *
     * @return true if within 90% of the limit, false otherwise
     */
    bool IsNearLimit() const;

    /**
     * @brief Reset all counters (for testing or restarting validation)
     *
     * Resets sent and received counters while maintaining the unvalidated state.
     */
    void Reset();

private:
    // State flag
    bool is_unvalidated_;               // True if address is not yet validated

    // Byte counters
    uint64_t sent_bytes_;               // Total bytes sent to unvalidated address
    uint64_t received_bytes_;           // Total bytes received from unvalidated address

    // Constants (RFC 9000 Section 8.1)
    static constexpr uint64_t kAmplificationFactor = 3;      // Maximum amplification factor
    static constexpr uint64_t kDefaultInitialCredit = 400;   // Initial credit (~allows 1200 bytes)
    static constexpr double kNearLimitThreshold = 0.9;       // 90% of limit for Retry consideration
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_CONTROLER_ANTI_AMPLIFICATION_CONTROLLER
