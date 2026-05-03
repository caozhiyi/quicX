#include "http3/qpack/blocked_registry.h"

namespace quicx {
namespace http3 {

QpackBlockedRegistry::QpackBlockedRegistry() {

}

QpackBlockedRegistry::~QpackBlockedRegistry() {

}

bool QpackBlockedRegistry::Add(uint64_t key, const std::function<void()>& retry_fn) {
    // RFC 9204 Section 2.1.2: Check if we've reached the max blocked streams limit
    if (!CanAddBlocked()) {
        return false;
    }
    pending_[key] = retry_fn;
    return true;
}

void QpackBlockedRegistry::Ack(uint64_t key) {
    auto it = pending_.find(key);
    if (it != pending_.end()) {
        auto fn = it->second;
        pending_.erase(it);
        if (fn) {
            fn();
        }
    }
}

void QpackBlockedRegistry::Remove(uint64_t key) {
    pending_.erase(key);
}

void QpackBlockedRegistry::NotifyAll() {
    for (auto& kv : pending_) {
        kv.second();
    }
    pending_.clear();
}

namespace {
// Return the iterator of the pending entry with the smallest key whose
// high 32 bits equal stream_id, or pending_.end() if none.
template <typename Map>
typename Map::iterator FindEarliestForStream(Map& pending, uint64_t stream_id) {
    typename Map::iterator found = pending.end();
    uint64_t best_key = 0;
    bool has_best = false;
    uint64_t hi_mask = stream_id << 32;
    for (auto it = pending.begin(); it != pending.end(); ++it) {
        if ((it->first >> 32) != stream_id) {
            continue;
        }
        if (!has_best || it->first < best_key) {
            best_key = it->first;
            found = it;
            has_best = true;
        }
    }
    (void)hi_mask;
    return found;
}
}  // namespace

bool QpackBlockedRegistry::AckByStreamId(uint64_t stream_id) {
    auto it = FindEarliestForStream(pending_, stream_id);
    if (it == pending_.end()) {
        return false;
    }
    auto fn = it->second;
    pending_.erase(it);
    if (fn) {
        fn();
    }
    return true;
}

bool QpackBlockedRegistry::RemoveByStreamId(uint64_t stream_id) {
    auto it = FindEarliestForStream(pending_, stream_id);
    if (it == pending_.end()) {
        return false;
    }
    pending_.erase(it);
    return true;
}

}
}

