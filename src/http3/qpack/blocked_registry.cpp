#include "http3/qpack/blocked_registry.h"

namespace quicx {
namespace http3 {

QpackBlockedRegistry& QpackBlockedRegistry::Instance() {
    static QpackBlockedRegistry inst;
    return inst;
}

void QpackBlockedRegistry::Add(uint64_t key, const std::function<void()>& retry_fn) {
    std::lock_guard<std::mutex> lock(mu_);
    pending_[key] = retry_fn;
}

void QpackBlockedRegistry::Ack(uint64_t key) {
    std::function<void()> fn;
    {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = pending_.find(key);
        if (it != pending_.end()) {
            fn = it->second;
            pending_.erase(it);
        }
    }
    if (fn) fn();
}

void QpackBlockedRegistry::Remove(uint64_t key) {
    std::lock_guard<std::mutex> lock(mu_);
    pending_.erase(key);
}

void QpackBlockedRegistry::NotifyAll() {
    std::vector<std::function<void()>> todo;
    {
        std::lock_guard<std::mutex> lock(mu_);
        for (auto& kv : pending_) todo.push_back(kv.second);
        pending_.clear();
    }
    for (auto& fn : todo) {
        if (fn) fn();
    }
}

}
}

// C linkage trampoline for simple call sites
extern "C" void QpackNotifyBlockedResume() {
    ::quicx::http3::QpackBlockedRegistry::Instance().NotifyAll();
}


