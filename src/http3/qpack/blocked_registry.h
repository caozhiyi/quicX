#ifndef HTTP3_QPACK_BLOCKED_REGISTRY
#define HTTP3_QPACK_BLOCKED_REGISTRY

#include <cstdint>
#include <functional>
#include <unordered_map>

namespace quicx {
namespace http3 {

class QpackBlockedRegistry {
public:
    QpackBlockedRegistry();
    ~QpackBlockedRegistry();
    
    // Set maximum number of blocked streams (from SETTINGS_QPACK_BLOCKED_STREAMS)
    void SetMaxBlockedStreams(uint64_t max_blocked) { max_blocked_streams_ = max_blocked; }
    
    // Get current number of blocked streams
    uint64_t GetBlockedCount() const { return pending_.size(); }
    
    // Check if we can add another blocked stream
    bool CanAddBlocked() const {
        return max_blocked_streams_ == 0 || pending_.size() < max_blocked_streams_;
    }
    
    // Enqueue a blocked header block by key (e.g., stream_id) with a retry closure
    // Returns false if max blocked streams limit reached
    bool Add(uint64_t key, const std::function<void()>& retry_fn);
    
    // Ack a section (by key), immediate retry and erase
    void Ack(uint64_t key);
    void Remove(uint64_t key);
    // Insert count increment — try to resume all
    void NotifyAll();

private:
    uint64_t max_blocked_streams_{0}; // 0 means unlimited (for testing/simple cases)
    std::unordered_map<uint64_t, std::function<void()>> pending_;
};

}
}

#endif


