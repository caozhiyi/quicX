#ifndef QUIC_CONNECTION_KEY_UPDATE_TRIGGER
#define QUIC_CONNECTION_KEY_UPDATE_TRIGGER

#include <cstdint>

namespace quicx {
namespace quic {

/**
 * @brief Key Update Trigger Manager
 *
 * RFC 9001 Section 6: Key Update
 * Manages when to trigger a key update during a QUIC connection.
 * Key updates can be triggered based on:
 * - Bytes sent threshold (e.g., after 512KB)
 * - Packet number threshold
 * - Time-based threshold
 */
class KeyUpdateTrigger {
public:
    KeyUpdateTrigger();
    ~KeyUpdateTrigger() = default;

    /**
     * @brief Enable key update triggering
     * @param enabled Whether to enable automatic key updates
     */
    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool IsEnabled() const { return enabled_; }

    /**
     * @brief Set bytes threshold for triggering key update
     * @param bytes_threshold Number of bytes to send before triggering
     */
    void SetBytesThreshold(uint64_t bytes_threshold) { bytes_threshold_ = bytes_threshold; }
    uint64_t GetBytesThreshold() const { return bytes_threshold_; }

    /**
     * @brief Set packet number threshold for triggering key update
     * @param pn_threshold Packet number to reach before triggering
     */
    void SetPacketNumberThreshold(uint64_t pn_threshold) { pn_threshold_ = pn_threshold; }
    uint64_t GetPacketNumberThreshold() const { return pn_threshold_; }

    /**
     * @brief Record bytes sent and check if key update should be triggered
     * @param bytes_sent Number of bytes sent
     * @return true if key update should be triggered
     */
    bool OnBytesSent(uint64_t bytes_sent);

    /**
     * @brief Record packet sent and check if key update should be triggered
     * @param packet_number Current packet number
     * @return true if key update should be triggered
     */
    bool OnPacketSent(uint64_t packet_number);

    /**
     * @brief Check if key update should be triggered (general check)
     * @return true if key update should be triggered based on any criteria
     */
    bool ShouldTriggerKeyUpdate() const;

    /**
     * @brief Mark that key update has been triggered
     */
    void MarkTriggered();

    /**
     * @brief Reset the trigger state for a new key update cycle
     */
    void Reset();

    /**
     * @brief Get the number of key updates performed
     */
    uint32_t GetKeyUpdateCount() const { return key_update_count_; }

private:
    bool enabled_;
    bool triggered_;
    uint32_t key_update_count_;

    // Bytes-based threshold
    uint64_t bytes_threshold_;
    uint64_t total_bytes_sent_;

    // Packet number-based threshold
    uint64_t pn_threshold_;
    uint64_t last_pn_at_update_;
    uint64_t current_pn_;
};

}  // namespace quic
}  // namespace quicx

#endif
