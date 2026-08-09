#include "quic/quicx/connection_rate_monitor.h"
#include "common/log/log.h"

namespace quicx {
namespace quic {

ConnectionRateMonitor::ConnectionRateMonitor(std::shared_ptr<common::IEventLoop> event_loop):
    event_loop_(event_loop) {}

ConnectionRateMonitor::~ConnectionRateMonitor() {
    // ~Timer() cancels, and cancelling is safe from any thread -- which matters
    // because this destructor runs from ServerWorker::Shutdown() on the
    // QuicServer-owner thread, after the worker's loop thread has been joined.
}

void ConnectionRateMonitor::RecordNewConnection() {
    // Lazily start the timer on first use (EventLoop is guaranteed to be initialized by now)
    auto loop = event_loop_.lock();
    if (loop && !timer_active_.load(std::memory_order_relaxed)) {
        StartTimer(loop);
    }
    current_count_.fetch_add(1, std::memory_order_relaxed);
}

uint32_t ConnectionRateMonitor::GetConnectionRate() const {
    return last_rate_.load(std::memory_order_relaxed);
}

uint32_t ConnectionRateMonitor::GetCurrentCount() const {
    return current_count_.load(std::memory_order_relaxed);
}

bool ConnectionRateMonitor::IsHighRate(uint32_t threshold) const {
    // Check both last completed rate and current accumulating count
    // This provides faster reaction to sudden spikes
    uint32_t rate = last_rate_.load(std::memory_order_relaxed);
    uint32_t current = current_count_.load(std::memory_order_relaxed);

    // If current count already exceeds threshold, we're in high rate mode
    return (rate >= threshold) || (current >= threshold);
}

void ConnectionRateMonitor::CalculateRate() {
    // Atomically swap current count to last rate and reset
    uint32_t count = current_count_.exchange(0, std::memory_order_relaxed);
    last_rate_.store(count, std::memory_order_relaxed);

    if (count > 0) {
        LOG_DEBUG("ConnectionRateMonitor: rate=%u connections/sec", count);
    }
}

void ConnectionRateMonitor::StartTimer(std::shared_ptr<common::IEventLoop> event_loop) {
    if (!event_loop) {
        LOG_WARN("ConnectionRateMonitor: cannot start timer without event loop");
        return;
    }

    if (timer_active_.exchange(true)) {
        // Timer already active
        return;
    }

    // Schedule repeating timer every 1000ms (1 second).
    //
    // This used to pass repeat=true to the id-based AddTimer, which never
    // actually re-registered anything: the rate was sampled exactly once, for the
    // lifetime of the process, and the closure stayed pinned in EventLoop's
    // bookkeeping. AddRepeatTimer really repeats.
    timer_ = event_loop->AddRepeatTimer(life_token_, [this]() { CalculateRate(); }, 1000);

    LOG_DEBUG("ConnectionRateMonitor: started rate calculation timer");
}

void ConnectionRateMonitor::StopTimer() {
    if (!timer_active_.exchange(false)) {
        // Timer not active
        return;
    }

    timer_.Cancel();
    LOG_DEBUG("ConnectionRateMonitor: stopped rate calculation timer");
}

}  // namespace quic
}  // namespace quicx
