#ifndef COMMON_TIMER_TIMER_NODE
#define COMMON_TIMER_TIMER_NODE

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>

namespace quicx {
namespace common {

// Internal timer node, owned by the wheel slot it lives in.
//
// This replaces the old public `TimerTask`, which callers could copy freely.
// That copyability was the root cause of the ownership confusion documented in
// docs/plans/2026-07-31-timer-redesign-design.md: the wheel stored a copy, the
// caller kept another copy, and a separate id->iterator hash map was needed to
// reconcile them.
struct TimerNode {
    // Index into TimerCore's slab, or kNoSlabEntry for a detached
    // (fire-and-forget) timer that has no handle and cannot be cancelled.
    static constexpr uint32_t kNoSlabEntry = std::numeric_limits<uint32_t>::max();

    std::function<void()> cb;

    // Lifetime guard. When has_owner is true and owner has expired, the
    // callback is skipped instead of invoked.
    std::weak_ptr<void> owner;
    bool has_owner = false;

    uint64_t deadline = 0;  // absolute ms
    uint32_t interval = 0;  // 0 = one-shot, otherwise the repeat period in ms
    uint32_t entry = kNoSlabEntry;
};

}  // namespace common
}  // namespace quicx

#endif  // COMMON_TIMER_TIMER_NODE
