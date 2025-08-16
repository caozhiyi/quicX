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
    // Enqueue a blocked header block by key (e.g., stream_id) with a retry closure
    void Add(uint64_t key, const std::function<void()>& retry_fn);
    // Ack a section (by key), immediate retry and erase
    void Ack(uint64_t key);
    void Remove(uint64_t key);
    // Insert count increment — try to resume all
    void NotifyAll();

private:
    std::unordered_map<uint64_t, std::function<void()>> pending_;
};

}
}

#endif


