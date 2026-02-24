#ifndef QUIC_CONNECTION_CONNECTION_RATE_MONITOR
#define QUIC_CONNECTION_CONNECTION_RATE_MONITOR

#include <atomic>
#include <cstdint>
#include <memory>

#include "common/network/if_event_loop.h"

namespace quicx {
namespace quic {

/**
 * @brief Connection rate monitor using sliding window algorithm.
 *
 * Tracks the number of new connection requests per second to help
 * make dynamic Retry decisions. Uses atomic operations for thread safety.
 *
 * Usage:
 *   1. Call RecordNewConnection() for each incoming Initial packet
 *   2. Call GetConnectionRate() or IsHighRate() to check current rate
 *   3. The monitor automatically resets the counter every second via timer
 *
 * Thread safety: All public methods are thread-safe.
 */
class ConnectionRateMonitor {
public:
    /**
     * @brief Construct a connection rate monitor.
     *
     * @param event_loop Event loop for scheduling the periodic timer.
     *                   If nullptr, the monitor will work without auto-reset
     *                   (useful for testing).
     */
    explicit ConnectionRateMonitor(std::shared_ptr<common::IEventLoop> event_loop = nullptr);
    
    ~ConnectionRateMonitor();

    /**
     * @brief Record a new connection request.
     *
     * Should be called when an Initial packet is received (before Retry decision).
     * This method is lock-free and safe to call from any thread.
     */
    void RecordNewConnection();

    /**
     * @brief Get the current connection rate.
     *
     * @return Connections per second (based on the last complete second).
     */
    uint32_t GetConnectionRate() const;

    /**
     * @brief Get the current window's connection count.
     *
     * @return Number of connections in the current (incomplete) second.
     */
    uint32_t GetCurrentCount() const;

    /**
     * @brief Check if the connection rate exceeds a threshold.
     *
     * @param threshold Rate threshold (connections/second).
     * @return true if current rate >= threshold.
     */
    bool IsHighRate(uint32_t threshold) const;

    /**
     * @brief Manually trigger a rate calculation (for testing).
     *
     * Normally called automatically by the timer every second.
     * This moves current_count_ to last_rate_ and resets current_count_.
     */
    void CalculateRate();

    /**
     * @brief Start the periodic rate calculation timer.
     *
     * Called automatically during construction if event_loop is provided.
     * Can be called manually if event_loop was not provided at construction.
     *
     * @param event_loop Event loop to schedule the timer on.
     */
    void StartTimer(std::shared_ptr<common::IEventLoop> event_loop);

    /**
     * @brief Stop the periodic rate calculation timer.
     */
    void StopTimer();

private:
    /** Counter for current time window (atomic for thread safety). */
    std::atomic<uint32_t> current_count_{0};
    
    /** Rate from the last complete time window. */
    std::atomic<uint32_t> last_rate_{0};
    
    /** Event loop for timer scheduling. */
    std::shared_ptr<common::IEventLoop> event_loop_;
    
    /** Timer ID for the periodic rate calculation. */
    uint64_t timer_id_{0};
    
    /** Flag to track if timer is active. */
    std::atomic<bool> timer_active_{false};
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_CONNECTION_RATE_MONITOR
