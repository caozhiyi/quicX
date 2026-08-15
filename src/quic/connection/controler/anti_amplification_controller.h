#ifndef QUIC_CONNECTION_CONTROLER_ANTI_AMPLIFICATION_CONTROLLER
#define QUIC_CONNECTION_CONTROLER_ANTI_AMPLIFICATION_CONTROLLER

#include <atomic>
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
 * - Call TryCharge() before sending each datagram (atomic check-and-debit)
 * - Call ExitUnvalidatedState() when address is validated
 *
 * Thread safety: Thread-safe via lock-free atomics. is_unvalidated_ is an
 * acquire/release flag; sent_bytes_ is committed with a CAS so that TryCharge()'s
 * check-and-debit is atomic (no TOCTOU between the budget check and the debit);
 * received_bytes_ is a monotonically increasing counter read with relaxed
 * ordering, which is safe because reading a slightly stale (smaller) value only
 * makes the 3x budget more conservative.
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
     * Test-facing accessor kept for unit tests. Production code should use
     * TryCharge(), which debits atomically. No-op once validated.
     *
     * @param bytes Number of bytes sent
     */
    void OnBytesSent(uint64_t bytes);

    /**
     * @brief Atomically check the budget AND debit it for `bytes`.
     *
     * A single CAS on sent_bytes_ so concurrent senders cannot both pass the
     * budget check and then over-debit past the 3x limit. Returns true and
     * commits the debit, or false (budget exhausted) without charging.
     *
     * @param bytes Size of the datagram about to be sent
     * @return true if the debit succeeded (send allowed), false otherwise
     */
    bool TryCharge(uint64_t bytes);

    /**
     * @brief Check if we can send a packet of given size
     *
     * Read-only variant kept for unit tests. Production code should use
     * TryCharge() to avoid the check/charge TOCTOU window.
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
    bool IsUnvalidated() const { return is_unvalidated_.load(std::memory_order_acquire); }

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
    uint64_t GetBytesSent() const { return sent_bytes_.load(std::memory_order_relaxed); }

    /**
     * @brief Get total bytes received from unvalidated address
     *
     * @return Total bytes received
     */
    uint64_t GetBytesReceived() const { return received_bytes_.load(std::memory_order_relaxed); }

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
    // State flag. acquire/release: once validation flips it false, all later
    // sends observe "unrestricted" immediately.
    std::atomic<bool> is_unvalidated_{false};  // True if address is not yet validated

    // Byte counters. sent_bytes_ is the only field that needs an atomic commit
    // (via TryCharge's CAS); received_bytes_ is monotonically increasing and is
    // read with relaxed ordering, as a stale (smaller) value only tightens the
    // 3x budget, keeping us safely within RFC 9000 Section 8.1.
    std::atomic<uint64_t> sent_bytes_{0};      // Total bytes sent to unvalidated address
    std::atomic<uint64_t> received_bytes_{0};  // Total bytes received from unvalidated address

    // Constants (RFC 9000 Section 8.1)
    static constexpr uint64_t kAmplificationFactor = 3;     // Maximum amplification factor
    static constexpr uint64_t kDefaultInitialCredit = 400;  // Initial credit (~allows 1200 bytes)
    static constexpr double kNearLimitThreshold = 0.9;      // 90% of limit for Retry consideration
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_CONTROLER_ANTI_AMPLIFICATION_CONTROLLER
