#include "common/log/log.h"
#include "quic/quicx/ip_rate_limiter.h"

namespace quicx {
namespace quic {

IPRateLimiter::IPRateLimiter(uint32_t max_cache_size,
                             uint32_t rate_threshold,
                             uint32_t window_seconds)
    : max_cache_size_(max_cache_size),
      rate_threshold_(rate_threshold),
      window_duration_(window_seconds) {
    common::LOG_DEBUG("IPRateLimiter: initialized with cache_size=%u, threshold=%u, window=%us",
                      max_cache_size, rate_threshold, window_seconds);
}

void IPRateLimiter::RecordConnection(const common::Address& addr) {
    RecordConnection(addr.GetIp());
}

void IPRateLimiter::RecordConnection(const std::string& ip) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    IPEntry& entry = GetOrCreateEntry(ip);
    
    // Check if window has expired and reset if needed
    CheckAndResetWindow(entry);
    
    // Increment count
    entry.count++;
    
    if (entry.count == rate_threshold_) {
        common::LOG_WARN("IPRateLimiter: IP %s reached threshold (%u connections in window)",
                         ip.c_str(), rate_threshold_);
    }
}

bool IPRateLimiter::IsSuspicious(const common::Address& addr) {
    return IsSuspicious(addr.GetIp());
}

bool IPRateLimiter::IsSuspicious(const std::string& ip) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = ip_map_.find(ip);
    if (it == ip_map_.end()) {
        return false;  // Unknown IP, not suspicious
    }
    
    IPEntry& entry = *it->second;
    
    // Check if window has expired
    if (CheckAndResetWindow(entry)) {
        return false;  // Window reset, count is now 0
    }
    
    // Move to front of LRU
    TouchEntry(it->second);
    
    return entry.count >= rate_threshold_;
}

uint32_t IPRateLimiter::GetConnectionCount(const std::string& ip) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = ip_map_.find(ip);
    if (it == ip_map_.end()) {
        return 0;
    }
    
    IPEntry& entry = *it->second;
    
    // Check if window has expired
    if (CheckAndResetWindow(entry)) {
        return 0;
    }
    
    return entry.count;
}

size_t IPRateLimiter::GetCacheSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lru_list_.size();
}

void IPRateLimiter::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    lru_list_.clear();
    ip_map_.clear();
    common::LOG_DEBUG("IPRateLimiter: cache cleared");
}

void IPRateLimiter::CleanupExpired() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto now = std::chrono::steady_clock::now();
    size_t removed = 0;
    
    // Remove expired entries from the back (oldest)
    while (!lru_list_.empty()) {
        auto& entry = lru_list_.back();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - entry.window_start);
        
        if (elapsed >= window_duration_ * 2) {
            // Entry is very stale (2x window duration), remove it
            ip_map_.erase(entry.ip);
            lru_list_.pop_back();
            removed++;
        } else {
            break;  // Rest of the list is newer
        }
    }
    
    if (removed > 0) {
        common::LOG_DEBUG("IPRateLimiter: cleaned up %zu expired entries", removed);
    }
}

void IPRateLimiter::UpdateConfig(uint32_t rate_threshold, uint32_t window_seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    rate_threshold_ = rate_threshold;
    window_duration_ = std::chrono::seconds(window_seconds);
    common::LOG_DEBUG("IPRateLimiter: config updated threshold=%u, window=%us",
                      rate_threshold, window_seconds);
}

IPRateLimiter::IPEntry& IPRateLimiter::GetOrCreateEntry(const std::string& ip) {
    auto it = ip_map_.find(ip);
    if (it != ip_map_.end()) {
        // Entry exists, move to front of LRU
        TouchEntry(it->second);
        return *it->second;
    }
    
    // Evict if needed before adding new entry
    EvictIfNeeded();
    
    // Create new entry at front of LRU list
    lru_list_.emplace_front(ip);
    ip_map_[ip] = lru_list_.begin();
    
    return lru_list_.front();
}

void IPRateLimiter::TouchEntry(std::list<IPEntry>::iterator it) {
    if (it != lru_list_.begin()) {
        // Move to front
        lru_list_.splice(lru_list_.begin(), lru_list_, it);
    }
}

void IPRateLimiter::EvictIfNeeded() {
    while (lru_list_.size() >= max_cache_size_) {
        // Remove from back (least recently used)
        auto& entry = lru_list_.back();
        common::LOG_DEBUG("IPRateLimiter: evicting IP %s (LRU)", entry.ip.c_str());
        ip_map_.erase(entry.ip);
        lru_list_.pop_back();
    }
}

bool IPRateLimiter::CheckAndResetWindow(IPEntry& entry) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - entry.window_start);
    
    if (elapsed >= window_duration_) {
        // Window expired, reset
        entry.count = 0;
        entry.window_start = now;
        return true;
    }
    
    return false;
}

}  // namespace quic
}  // namespace quicx
