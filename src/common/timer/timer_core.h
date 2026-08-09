#ifndef COMMON_TIMER_TIMER_CORE
#define COMMON_TIMER_TIMER_CORE

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <limits>
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "common/timer/timer_node.h"

namespace quicx {
namespace common {

/**
 * @brief Three-level hierarchical timing wheel plus a slab handle table.
 *
 * Wheel geometry (identical to the previous TimingWheelTimer so every existing
 * wheel test, including the Bug #21 regressions, keeps applying):
 *   L0: 256 slots x     1 ms =       256 ms
 *   L1:  64 slots x   256 ms =    16 384 ms
 *   L2:  64 slots x 16384 ms = 1 048 576 ms
 *   plus an overflow list for anything beyond L2.
 *
 * Handle model
 * ------------
 * Every cancellable timer owns exactly one slab Entry, and the Entry's lifetime
 * equals the *handle's* lifetime rather than the arming's lifetime: after a
 * one-shot fires, its node is parked in `idle_` and its Entry stays allocated
 * (level == -1) until the handle releases it. Because the handle is move-only
 * and is the Entry's sole owner, no stale duplicate handle can exist. That
 * removes the ABA problem and, with it, the need for a globally unique timer id
 * -- which is what the old implementation needed its
 * `unordered_map<uint64_t, list_iterator>` for. That map cost ~56 ns per insert
 * (measured); a slab index costs one array access.
 *
 * `gen` is therefore not an ABA guard but a plain release token: it makes
 * use-after-release a safe no-op instead of undefined behaviour. It is bumped
 * only by ReleaseEntry() and Clear(), never by CancelLocal(), so a handle can
 * cancel and then still release its own entry.
 *
 * Threading
 * ---------
 *   Arm / ArmDetached / Rearm / CancelLocal / ReleaseEntry / Run / MinTime /
 *   Clear : loop thread only.
 *   CancelRemote : any thread.
 *
 * The hot path takes no lock and performs no atomic read-modify-write. The
 * cross-thread cancel path pushes onto a mutex-protected queue that the loop
 * thread drains at the top of Run() and again before invoking any callback
 * while the queue is non-empty. The only cost paid on the hot path is one
 * relaxed atomic load per fired timer.
 *
 * Cross-thread cancel guarantees that no *new* callback starts, but it cannot
 * interrupt a callback that is already executing -- the same guarantee level as
 * Linux `del_timer` (as opposed to `del_timer_sync`). That residual case is
 * covered by the owner guard.
 */
class TimerCore {
public:
    static constexpr uint32_t kNoEntry = TimerNode::kNoSlabEntry;

    static constexpr uint32_t kL0Bits = 8;
    static constexpr uint32_t kL1Bits = 6;
    static constexpr uint32_t kL2Bits = 6;

    static constexpr uint32_t kL0Size = 1u << kL0Bits;
    static constexpr uint32_t kL1Size = 1u << kL1Bits;
    static constexpr uint32_t kL2Size = 1u << kL2Bits;

    static constexpr uint32_t kL0Mask = kL0Size - 1;
    static constexpr uint32_t kL1Mask = kL1Size - 1;
    static constexpr uint32_t kL2Mask = kL2Size - 1;

    static constexpr uint64_t kL0Range = kL0Size;
    static constexpr uint64_t kL1Range = kL0Range * kL1Size;
    static constexpr uint64_t kL2Range = kL1Range * kL2Size;

    using Slot = std::list<TimerNode>;

    TimerCore() = default;
    ~TimerCore() = default;

    TimerCore(const TimerCore&) = delete;
    TimerCore& operator=(const TimerCore&) = delete;

    // ---- cancellable timers (loop thread) ----

    /**
     * @brief Arm a cancellable timer.
     *
     * @param cb          callback to invoke on expiry
     * @param owner       lifetime guard (see TimerNode::owner)
     * @param has_owner   whether `owner` should be honoured
     * @param delay_ms    delay from `now`
     * @param interval_ms 0 for one-shot, otherwise the repeat period
     * @param now         current time in ms
     * @param out_gen     receives the release token for the returned index
     * @return slab index, or kNoEntry on failure
     */
    uint32_t Arm(std::function<void()> cb, std::weak_ptr<void> owner, bool has_owner, uint32_t delay_ms,
        uint32_t interval_ms, uint64_t now, uint32_t& out_gen);

    /**
     * @brief Reschedule an existing timer in place: the node is spliced between
     *        slots, never freed and reallocated, and the owner guard is not
     *        re-copied. Works both for an armed timer and for one that has
     *        already fired (its node is parked, not destroyed).
     */
    bool Rearm(uint32_t index, uint32_t gen, uint32_t delay_ms, uint64_t now);

    /// Cancel without releasing the entry. Returns true if a pending timer was
    /// actually cancelled. Destroys the callback immediately.
    bool CancelLocal(uint32_t index, uint32_t gen);

    /// Release the slab entry; invalidates the (index, gen) pair for good.
    void ReleaseEntry(uint32_t index, uint32_t gen);

    bool IsActive(uint32_t index, uint32_t gen) const;

    // ---- cancellable timers (any thread) ----
    void CancelRemote(uint32_t index, uint32_t gen);

    // ---- fire-and-forget timers (loop thread) ----
    void ArmDetached(std::function<void()> cb, uint32_t delay_ms, uint64_t now);

    // ---- engine ----

    /// Milliseconds until the earliest deadline; 0 if due, -1 if nothing armed.
    /// The value is always exact (never a lower bound).
    int32_t MinTime(uint64_t now);

    void Run(uint64_t now);

    /// Drop every pending node and closure, and invalidate all outstanding
    /// handles. Intended for teardown after the loop has stopped.
    void Clear();

    bool Empty() const { return total_tasks_ == 0; }
    /// Number of timers currently armed (parked or cancelled ones excluded).
    uint32_t PendingCount() const { return total_tasks_; }

    void BindLoopThread(std::thread::id id) { loop_tid_ = id; }
    bool IsInLoopThread() const { return std::this_thread::get_id() == loop_tid_; }

private:
    struct Entry {
        uint32_t gen = 1;     // release token
        int8_t level = -1;    // -1 = node parked in idle_ (or absent); 0..2 wheel; 3 overflow
        uint32_t slot = 0;
        Slot::iterator it;
        uint32_t next_free = kNoEntry;
        bool in_use = false;         // a live handle references this entry
        bool has_node = false;       // a TimerNode exists for this entry
    };

    uint32_t AllocEntry();
    Slot& ListOf(int8_t level, uint32_t slot);
    void ComputeDest(uint64_t deadline, uint64_t reference, int8_t& level, uint32_t& slot) const;

    // Attach the node currently parked for `index` into the wheel.
    void AttachFromIdle(uint32_t index, uint64_t reference);
    // Detach an armed node back into idle_ (keeping it) or destroy it.
    void DetachArmed(uint32_t index, bool destroy);
    void DestroyParked(uint32_t index);

    void OnNodeArmed(uint64_t deadline);
    void OnNodeDisarmed(uint64_t deadline);

    void DrainRemoteCancels();
    void Cascade(int level, uint32_t slot);
    void Tick(uint64_t now);
    void FireSlot(uint32_t c0);
    uint64_t EarliestDeadline(uint32_t* holders) const;

    void SetL0Bit(uint32_t s) { wheel0_occ_[s >> 6] |= (1ull << (s & 63)); }
    void ClearL0Bit(uint32_t s) { wheel0_occ_[s >> 6] &= ~(1ull << (s & 63)); }
    uint32_t NextL0From(uint32_t from) const;

    std::array<Slot, kL0Size> wheel0_;
    std::array<Slot, kL1Size> wheel1_;
    std::array<Slot, kL2Size> wheel2_;
    Slot overflow_;
    // Nodes that belong to a live handle but are not currently armed (fired
    // one-shots waiting for a Rearm or for their handle to go away).
    Slot idle_;

    std::array<uint64_t, 4> wheel0_occ_ = {0, 0, 0, 0};

    std::vector<Entry> slab_;
    uint32_t free_head_ = kNoEntry;

    std::mutex remote_mu_;
    std::vector<std::pair<uint32_t, uint32_t>> remote_pending_;
    std::atomic<uint32_t> remote_pending_count_{0};

    // Exact minimum-deadline cache. `min_holders_` counts how many armed nodes
    // share `min_deadline_`, so removing one of several holders does not force a
    // rescan. A rescan happens only when the last holder disappears.
    uint64_t min_deadline_ = 0;
    uint32_t min_holders_ = 0;
    bool min_valid_ = false;

    uint64_t current_ms_ = 0;
    bool initialized_ = false;
    uint32_t total_tasks_ = 0;
    uint32_t firing_depth_ = 0;
    std::thread::id loop_tid_ = std::this_thread::get_id();
};

}  // namespace common
}  // namespace quicx

#endif  // COMMON_TIMER_TIMER_CORE
