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

}
}

