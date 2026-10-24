#ifndef QUIC_QUICX_PER_IP_CONNECTION_COUNTER
#define QUIC_QUICX_PER_IP_CONNECTION_COUNTER

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include "common/log/log.h"

namespace quicx {
namespace quic {

/**
 * @brief Process-wide live-connection count per source IP.
 *
 * P0-S10: ServerWorker used to keep its own per-IP map, so with N workers the
 * effective cap was N x max_connections_per_ip_ — each worker counted its own
 * connections against the full limit, and a client spraying handshakes got
 * load-balanced onto workers that each still had budget. One process-wide
 * table (quicX runs multi-worker inside a single process) makes the operator's
 * limit mean what it says.
 *
 * Access is single-threaded per worker event loop but multiple workers run
 * concurrently, hence the mutex. Contention is a non-issue at admission rates:
 * the critical section is two map lookups.
 */
class PerIpConnectionCounter {
public:
    static PerIpConnectionCounter& Instance() {
        static PerIpConnectionCounter instance;
        return instance;
    }

    // Returns false when |ip| is already at |limit| live connections.
    // limit == 0 disables the check.
    bool CanAdmit(const std::string& ip, uint32_t limit) {
        if (limit == 0) {
            return true;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = counts_.find(ip);
        if (it != counts_.end() && it->second >= limit) {
            LOG_WARN("per-ip connection limit reached. ip:%s, count:%u, limit:%u", ip.c_str(), it->second, limit);
            return false;
        }
        return true;
    }

    void Increment(const std::string& ip) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++counts_[ip];
    }

    void Decrement(const std::string& ip) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = counts_.find(ip);
        if (it == counts_.end()) {
            return;  // unbalanced release: tolerate, never underflow
        }
        if (--it->second == 0) {
            // Drop empty buckets, otherwise the table grows without bound as
            // distinct source IPs come and go.
            counts_.erase(it);
        }
    }

    // Test hook: clears the table between unit tests.
    void ResetForTesting() {
        std::lock_guard<std::mutex> lock(mutex_);
        counts_.clear();
    }

private:
    PerIpConnectionCounter() = default;

    std::mutex mutex_;
    std::unordered_map<std::string, uint32_t> counts_;
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_QUICX_PER_IP_CONNECTION_COUNTER
