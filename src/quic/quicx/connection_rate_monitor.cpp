#include "common/log/log.h"
#include "quic/quicx/connection_rate_monitor.h"

namespace quicx {
namespace quic {

ConnectionRateMonitor::ConnectionRateMonitor(std::shared_ptr<common::IEventLoop> event_loop)
    : event_loop_(event_loop) {

}

ConnectionRateMonitor::~ConnectionRateMonitor() {
    // Intentionally do NOT call StopTimer() here:
    //   * StopTimer() invokes IEventLoop::RemoveTimer(), which is guarded
    //     by AssertInLoopThread() and aborts when called from anywhere
    //     other than the worker's event-loop thread.
    //   * This destructor runs from ServerWorker::Shutdown() on the
    //     QuicServer-owner thread (after the worker thread has been
    //     Stop()+Join()'d — see QuicServer::~QuicServer comments).
    //   * Since the event loop is no longer running, the periodic timer
    //     cannot fire, and its bookkeeping inside the timer wheel is
    //     released when the EventLoop itself is destroyed.
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
    
    // Schedule repeating timer every 1000ms (1 second)
    timer_id_ = event_loop->AddTimer(
        [this]() {
            CalculateRate();
        },
        1000,   // 1 second interval
        true    // repeat
    );
    
    LOG_DEBUG("ConnectionRateMonitor: started rate calculation timer (id=%llu)", timer_id_);
}

void ConnectionRateMonitor::StopTimer() {
    if (!timer_active_.exchange(false)) {
        // Timer not active
        return;
    }
    
    auto loop = event_loop_.lock();
    if (loop && timer_id_ != 0) {
        loop->RemoveTimer(timer_id_);
        LOG_DEBUG("ConnectionRateMonitor: stopped rate calculation timer (id=%llu)", timer_id_);
        timer_id_ = 0;
    }
}

}  // namespace quic
}  // namespace quicx
