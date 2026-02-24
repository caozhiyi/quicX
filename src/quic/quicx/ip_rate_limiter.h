#ifndef QUIC_CONNECTION_IP_RATE_LIMITER
#define QUIC_CONNECTION_IP_RATE_LIMITER

#include <chrono>
#include <cstdint>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>

#include "common/network/address.h"

namespace quicx {
namespace quic {

/**
 * @brief Per-IP rate limiter using LRU cache.
 *
 * Tracks connection attempts from individual IP addresses to detect
 * potential DDoS attacks or abusive clients. Uses an LRU cache to
 * limit memory usage while maintaining accuracy for active IPs.
 *
 * Features:
 *   - LRU eviction policy to bound memory usage
 *   - Configurable time window and rate threshold
 *   - Automatic expiration of stale entries
 *   - Thread-safe operations
 *
 * Usage:
 *   1. Call RecordConnection() for each incoming connection from an IP
 *   2. Call IsSuspicious() to check if an IP should trigger Retry
 */
class IPRateLimiter {
public:
    /**
     * @brief Construct an IP rate limiter.
     *
     * @param max_cache_size Maximum number of IP entries to track (LRU eviction).
     * @param rate_threshold Connections per time window that mark IP as suspicious.
     * @param window_seconds Time window for rate calculation (seconds).
     */
    IPRateLimiter(uint32_t max_cache_size = 10000,
                  uint32_t rate_threshold = 100,
                  uint32_t window_seconds = 60);
    
    ~IPRateLimiter() = default;

    /**
     * @brief Record a connection attempt from an IP address.
     *
     * @param addr Client address.
     */
    void RecordConnection(const common::Address& addr);

    /**
     * @brief Record a connection attempt from an IP string.
     *
     * @param ip IP address string.
     */
    void RecordConnection(const std::string& ip);

    /**
     * @brief Check if an IP address is suspicious.
     *
     * An IP is considered suspicious if it has exceeded the rate threshold
     * within the configured time window.
     *
     * @param addr Client address.
     * @return true if the IP is suspicious and should trigger Retry.
     */
    bool IsSuspicious(const common::Address& addr);

    /**
     * @brief Check if an IP address is suspicious.
     *
     * @param ip IP address string.
     * @return true if the IP is suspicious and should trigger Retry.
     */
    bool IsSuspicious(const std::string& ip);

    /**
     * @brief Get the current connection count for an IP.
     *
     * @param ip IP address string.
     * @return Number of connections in the current time window.
     */
    uint32_t GetConnectionCount(const std::string& ip);

    /**
     * @brief Get the number of tracked IP addresses.
     *
     * @return Current cache size.
     */
    size_t GetCacheSize() const;

    /**
     * @brief Clear all tracked IP addresses.
     *
     * Useful for testing or resetting state.
     */
    void Clear();

    /**
     * @brief Clean up expired entries.
     *
     * Called automatically during operations, but can be invoked manually.
     */
    void CleanupExpired();

    /**
     * @brief Update configuration parameters.
     *
     * @param rate_threshold New rate threshold.
     * @param window_seconds New time window.
     */
    void UpdateConfig(uint32_t rate_threshold, uint32_t window_seconds);

private:
    /**
     * @brief Entry for tracking IP connection rate.
     */
    struct IPEntry {
        std::string ip;
        uint32_t count;
        std::chrono::steady_clock::time_point window_start;
        
        IPEntry(const std::string& ip_addr)
            : ip(ip_addr), count(0), window_start(std::chrono::steady_clock::now()) {}
    };

    /**
     * @brief Get or create an entry for an IP (internal, caller must hold lock).
     *
     * @param ip IP address string.
     * @return Reference to the IP entry.
     */
    IPEntry& GetOrCreateEntry(const std::string& ip);

    /**
     * @brief Move an entry to the front of the LRU list (internal, caller must hold lock).
     *
     * @param it Iterator to the entry in the LRU list.
     */
    void TouchEntry(std::list<IPEntry>::iterator it);

    /**
     * @brief Evict the least recently used entry if cache is full (internal, caller must hold lock).
     */
    void EvictIfNeeded();

    /**
     * @brief Check and reset window if expired (internal, caller must hold lock).
     *
     * @param entry Entry to check.
     * @return true if the window was reset.
     */
    bool CheckAndResetWindow(IPEntry& entry);

private:
    /** Maximum cache size (LRU eviction threshold). */
    uint32_t max_cache_size_;
    
    /** Rate threshold for marking IP as suspicious. */
    uint32_t rate_threshold_;
    
    /** Time window for rate calculation. */
    std::chrono::seconds window_duration_;
    
    /** LRU list of IP entries (front = most recent). */
    std::list<IPEntry> lru_list_;
    
    /** Map from IP to iterator in LRU list for O(1) lookup. */
    std::unordered_map<std::string, std::list<IPEntry>::iterator> ip_map_;
    
    /** Mutex for thread safety. */
    mutable std::mutex mutex_;
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_IP_RATE_LIMITER
