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
    by_stream_[key >> 32].insert(key);
    return true;
}

void QpackBlockedRegistry::Ack(uint64_t key) {
    auto it = pending_.find(key);
    if (it != pending_.end()) {
        auto fn = it->second;
        pending_.erase(it);
        auto sit = by_stream_.find(key >> 32);
        if (sit != by_stream_.end()) {
            sit->second.erase(key);
            if (sit->second.empty()) {
                by_stream_.erase(sit);
            }
        }
        if (fn) {
            fn();
        }
    }
}

void QpackBlockedRegistry::Remove(uint64_t key) {
    auto it = pending_.find(key);
    if (it == pending_.end()) {
        return;
    }
    pending_.erase(it);
    auto sit = by_stream_.find(key >> 32);
    if (sit != by_stream_.end()) {
        sit->second.erase(key);
        if (sit->second.empty()) {
            by_stream_.erase(sit);
        }
    }
}

void QpackBlockedRegistry::NotifyAll() {
    // Swap out the current pending set so that callbacks may re-Add
    // themselves (or other entries) without iterator invalidation and,
    // crucially, without being silently dropped if their decode still
    // can't proceed.  RFC 9204 §2.1.4: a header block remains blocked
    // until the decoder's Required Insert Count is met; one IIC may not
    // be enough to unblock every pending section.
    std::unordered_map<uint64_t, std::function<void()>> snapshot;
    snapshot.swap(pending_);
    by_stream_.clear();
    for (auto& kv : snapshot) {
        if (kv.second) {
            kv.second();
        }
    }
}

bool QpackBlockedRegistry::AckByStreamId(uint64_t stream_id) {
    auto sit = by_stream_.find(stream_id);
    if (sit == by_stream_.end() || sit->second.empty()) {
        return false;
    }
    // Earliest outstanding section == smallest key in the per-stream set.
    uint64_t key = *sit->second.begin();
    sit->second.erase(sit->second.begin());
    if (sit->second.empty()) {
        by_stream_.erase(sit);
    }

    auto pit = pending_.find(key);
    if (pit == pending_.end()) {
        // Should not happen when indices are in sync, but stay defensive.
        return false;
    }
    auto fn = pit->second;
    pending_.erase(pit);
    if (fn) {
        fn();
    }
    return true;
}

bool QpackBlockedRegistry::RemoveByStreamId(uint64_t stream_id) {
    auto sit = by_stream_.find(stream_id);
    if (sit == by_stream_.end() || sit->second.empty()) {
        return false;
    }
    uint64_t key = *sit->second.begin();
    sit->second.erase(sit->second.begin());
    if (sit->second.empty()) {
        by_stream_.erase(sit);
    }
    pending_.erase(key);
    return true;
}

}
}
