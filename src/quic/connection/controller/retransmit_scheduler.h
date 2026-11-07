#ifndef QUIC_CONNECTION_CONTROLER_RETRANSMIT_SCHEDULER
#define QUIC_CONNECTION_CONTROLER_RETRANSMIT_SCHEDULER

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <utility>

#include "common/timer/if_timer_scheduler.h"

namespace quicx {
namespace quic {

/**
 * @brief An entity that owns the retransmission of its own unacknowledged data
 *        (P0-C10, INV1): CryptoStream per encryption level, and in principle
 *        any SendStream.
 *
 * The scheduler never touches packets or bytes — it only tells an owner "the
 * data you still have outstanding has been outstanding for one base PTO";
 * what to put back on the wire is the owner's business (frame-level requeue,
 * duplicates harmless by design).
 */
class IRetransmitOwner {
public:
    virtual ~IRetransmitOwner() = default;

    /**
     * @brief The slot's deadline expired. NOT a loss event — implementations
     *        must requeue as kProbe, never report congestion.
     *
     * The scheduler has ALREADY re-registered the slot at (now + base PTO)
     * before calling, so a handler that has more to do needs nothing further;
     * a handler that finds nothing outstanding calls RetransmitScheduler::
     * Remove(), and one that wants a different deadline calls Update().
     */
    virtual void OnRetransmitDeadline(uint32_t slot, uint64_t now_ms) = 0;
};

/**
 * @brief X3 slow-path scheduler (docs/internal/p0_c10_unified_retransmit_design.md D1).
 *
 * One timer for the whole connection, armed at
 *     min over registered (owner, slot) of its deadline
 * — structurally the same as SendControl::SetLossDetectionTimer (RFC 9002
 * A.8's GetPtoTimeAndSpace is the same "earliest of several" shape), and the
 * timer node is reused through Timer::Rearm. Entries live in an ordered set,
 * so the min is O(1), insert/erase/update O(log N); there is NO periodic scan
 * of owners (the SustainedLoad risk in the design doc).
 *
 * Why it exists (lesson L1, 2026-10-26): with ONLY frame-level requeue on
 * ACK-driven loss plus the connection PTO, recovery cadence under 30% loss is
 * the PTO's exponential backoff (750ms -> 1.5s -> 3s ...), and the
 * probability that every retransmission of a handshake flight dies grows with
 * the gap. The baseline survived on an ARITHMETIC cadence: each retransmission
 * restarted a fixed base-PTO interval. This scheduler provides exactly that,
 * per owner instead of per packet:
 *   - deadline = (when the owner's oldest outstanding data was last put on the
 *     wire, or last requeued) + BASE PTO — never the backed-off PTO (INV2);
 *   - new sends do NOT push an armed deadline out (a chatty owner cannot
 *     starve its own old data — the keep-alive-PING lesson of 2026-10-27);
 *   - after a fire the slot restarts from now, so cadence is arithmetic.
 * Fairness (INV3): entries fire strictly in deadline order, so one busy owner
 * cannot delay another's due deadline (the D4 probe-space-starvation lesson).
 * INV4: this class does not declare loss and does not touch cwnd.
 *
 * Thread model: single event-loop thread, like everything in a connection.
 */
class RetransmitScheduler {
public:
    using Clock = std::function<uint64_t()>;          ///< milliseconds, monotonic
    using IntervalProvider = std::function<uint32_t()>;  ///< current BASE PTO, milliseconds

    /**
     * @param scheduler  timer facility; null makes the scheduler a bookkeeping
     *                   no-op (unit tests of owners that do not care).
     * @param now        injectable clock; also what Rearm is told, so tests
     *                   driving a virtual clock stay in one time domain.
     * @param base_pto   base (NOT backed-off) PTO interval in ms.
     */
    RetransmitScheduler(
        std::shared_ptr<common::ITimerScheduler> scheduler, Clock now, IntervalProvider base_pto);
    ~RetransmitScheduler();

    RetransmitScheduler(const RetransmitScheduler&) = delete;
    RetransmitScheduler& operator=(const RetransmitScheduler&) = delete;

    /// Lifetime guard for the timer callback (see ITimerScheduler::AddTimer).
    /// Bound late: the owning connection only becomes shared_ptr-managed after
    /// construction (SendControl::Bind). An unbound (expired) guard would make
    /// TimerCore skip the callback forever, so no timer is armed until the
    /// token is set; entries registered earlier are armed at that point.
    void SetLifeToken(std::weak_ptr<void> life);

    /**
     * @brief Register (owner, slot) at now + base PTO if it is not registered
     *        yet; a no-op if it is (the older, earlier deadline is kept so
     *        continuous sending cannot defer outstanding data).
     */
    void Arm(IRetransmitOwner* owner, uint32_t slot);

    /// Set the slot's deadline to now + base PTO, replacing any existing one.
    void Restart(IRetransmitOwner* owner, uint32_t slot);

    /// Set the slot's deadline to an absolute time (ms), replacing any existing one.
    void Update(IRetransmitOwner* owner, uint32_t slot, uint64_t deadline_ms);

    /// Unregister one slot. Idempotent.
    void Remove(IRetransmitOwner* owner, uint32_t slot);

    /// Unregister every slot of an owner (owner destructor). Idempotent.
    void RemoveOwner(IRetransmitOwner* owner);

    /// Drop everything and cancel the timer (connection closing).
    void Clear();

    bool IsArmed(const IRetransmitOwner* owner, uint32_t slot) const;
    size_t Size() const { return queue_.size(); }

    /// The scheduler's own clock (ms) and current BASE PTO. Owners that stamp
    /// when they put bytes on the wire MUST use this clock so their timestamps
    /// and the deadlines they hand back live in one time domain (tests drive a
    /// virtual clock).
    uint64_t Now() const { return now_ ? now_() : 0; }
    uint64_t IntervalMs() const;

    /// Earliest registered deadline in ms; 0 when nothing is registered.
    uint64_t NextDeadlineMs() const { return queue_.empty() ? 0 : queue_.begin()->deadline_ms; }

    /// Run everything due at `now` (the timer callback body; public so tests
    /// can drive it without a real clock).
    void RunDue(uint64_t now);

private:
    struct Entry {
        uint64_t deadline_ms;
        IRetransmitOwner* owner;
        uint32_t slot;
        bool operator<(const Entry& o) const {
            if (deadline_ms != o.deadline_ms) return deadline_ms < o.deadline_ms;
            if (owner != o.owner) return std::less<IRetransmitOwner*>()(owner, o.owner);
            return slot < o.slot;
        }
    };
    using Key = std::pair<IRetransmitOwner*, uint32_t>;

    void InsertOrReplace(IRetransmitOwner* owner, uint32_t slot, uint64_t deadline_ms);
    // Re-point the single timer at the current minimum (or cancel it).
    void Reschedule();

    std::shared_ptr<common::ITimerScheduler> scheduler_;
    Clock now_;
    IntervalProvider base_pto_;
    std::weak_ptr<void> life_;

    std::set<Entry> queue_;
    std::map<Key, uint64_t> index_;  // (owner, slot) -> deadline, for O(log N) replace/remove
    common::Timer timer_;
    // While RunDue dispatches, owner callbacks may Update/Remove; the single
    // Reschedule happens once at the end instead of per mutation.
    bool dispatching_ = false;
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_CONTROLER_RETRANSMIT_SCHEDULER
