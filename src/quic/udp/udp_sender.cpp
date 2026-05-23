#include "quic/udp/udp_sender.h"
#include "common/log/log.h"
#include <quicx/common/metrics.h>
#include <quicx/common/metrics_std.h>
#include "common/network/io_handle.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <queue>
#include <random>
#include <thread>

namespace quicx {
namespace quic {

// ============================================================
// Static fault-injection state
// ============================================================
std::atomic<uint32_t> UdpSender::drop_per_million_{0};
std::atomic<uint64_t> UdpSender::rate_limit_bps_{0};
std::atomic<uint32_t> UdpSender::egress_delay_ms_{0};

namespace {

// ---------- random ----------
// Thread-local RNG so concurrent senders don't contend on a global mutex
// and so different threads don't produce the same drop pattern.
inline uint32_t NextRandPerMillion() {
    thread_local std::mt19937 rng{std::random_device{}()};
    thread_local std::uniform_int_distribution<uint32_t> dist(0, 999'999);
    return dist(rng);
}

// ---------- token bucket ----------
//
// Single global bucket guarded by a mutex. The serialization is intentional:
// it mirrors a single shared bottleneck link in the simulator. Throughput
// is so low (1Mbps -> ~85 packets/s at MTU=1452) that lock contention is a
// non-issue.
//
// Bucket capacity = 2 * rate / 100 bytes (~20ms of burst credit). That is
// enough to absorb pacing-driven micro-bursts without breaking the average
// rate cap; longer bursts get tail-dropped, which is exactly the symptom
// the simulator induces.
class TokenBucket {
public:
    // Returns true when `bytes` were successfully consumed; returns false
    // (i.e. the caller should drop the packet) when the bucket lacks credit.
    // When the limiter is disabled (rate == 0) this is a fast no-op returning
    // true.
    bool TryConsume(size_t bytes, uint64_t rate_bps) {
        if (rate_bps == 0) {
            return true;
        }
        std::lock_guard<std::mutex> lock(mu_);
        const auto now = std::chrono::steady_clock::now();
        if (last_refill_.time_since_epoch().count() == 0) {
            last_refill_ = now;
            tokens_ = static_cast<double>(rate_bps) * 0.02;  // start full of 20ms credit
        }
        const auto elapsed_s =
            std::chrono::duration<double>(now - last_refill_).count();
        last_refill_ = now;

        const double cap = std::max(static_cast<double>(rate_bps) * 0.02, 1500.0);
        tokens_ = std::min(cap, tokens_ + elapsed_s * static_cast<double>(rate_bps));

        if (tokens_ >= static_cast<double>(bytes)) {
            tokens_ -= static_cast<double>(bytes);
            return true;
        }
        return false;
    }

    void Reset() {
        std::lock_guard<std::mutex> lock(mu_);
        tokens_ = 0.0;
        last_refill_ = std::chrono::steady_clock::time_point{};
    }

private:
    std::mutex mu_;
    double tokens_{0.0};
    std::chrono::steady_clock::time_point last_refill_{};
};

TokenBucket& Bucket() {
    static TokenBucket b;
    return b;
}

// ---------- delay queue ----------
//
// Lazy-started worker that re-emits buffered packets at their scheduled
// release time. Insertion order is preserved (FIFO + fixed delay = no
// reordering), which matches the simulator's deterministic delay link.
//
// We deliberately do NOT use the real send path's NetPacket buffer pool
// after enqueue: the NetPacket itself owns its data buffer via shared_ptr,
// so just keeping the shared_ptr alive in the queue is sufficient.
struct DelayedPacket {
    std::chrono::steady_clock::time_point release_at;
    std::shared_ptr<NetPacket> pkt;
    int32_t sock;
};

class DelayQueue {
public:
    static DelayQueue& Instance() {
        static DelayQueue q;
        return q;
    }

    // Enqueue a packet for delayed transmission. release_at is computed by
    // the caller so all packets enqueued in a tight loop share a consistent
    // baseline even if the worker thread wakes up slowly.
    void Enqueue(DelayedPacket&& dp) {
        EnsureWorker();
        {
            std::lock_guard<std::mutex> lock(mu_);
            q_.push(std::move(dp));
        }
        cv_.notify_one();
    }

    // Stop the worker and drop any still-buffered packets. Safe to call
    // multiple times. Called from ResetFaultInjection() so tests can be
    // sure no in-flight delayed packet pollutes the next test.
    void Drain() {
        std::unique_lock<std::mutex> lock(mu_);
        std::queue<DelayedPacket> empty;
        std::swap(q_, empty);
    }

private:
    DelayQueue() = default;
    ~DelayQueue() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            stop_ = true;
        }
        cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void EnsureWorker() {
        std::call_once(start_once_, [this]() {
            worker_ = std::thread(&DelayQueue::Run, this);
        });
    }

    void Run() {
        for (;;) {
            DelayedPacket dp;
            {
                std::unique_lock<std::mutex> lock(mu_);
                cv_.wait(lock, [this]() { return stop_ || !q_.empty(); });
                if (stop_ && q_.empty()) {
                    return;
                }
                // Sleep until the head of the queue is due. While sleeping,
                // a notify_one() from a newer (earlier-due) packet will only
                // re-evaluate at the next wakeup, but FIFO + fixed delay
                // guarantees the head is always the earliest-due so this is
                // fine.
                const auto now = std::chrono::steady_clock::now();
                if (q_.front().release_at > now) {
                    cv_.wait_until(lock, q_.front().release_at);
                    if (stop_ && q_.empty()) {
                        return;
                    }
                    if (q_.empty() || q_.front().release_at >
                        std::chrono::steady_clock::now()) {
                        continue;
                    }
                }
                dp = std::move(q_.front());
                q_.pop();
            }
            // Lock released. Do the actual sendto here so a slow syscall
            // doesn't block enqueue.
            EmitNow(dp);
        }
    }

    static void EmitNow(const DelayedPacket& dp) {
        if (!dp.pkt) {
            return;
        }
        auto buffer = dp.pkt->GetData();
        if (!buffer) {
            return;
        }
        auto span = buffer->GetReadableSpan();
        const int32_t sock = dp.sock;
        if (sock <= 0) {
            return;
        }
        auto ret = common::SendTo(sock, (const char*)span.GetStart(),
                                  span.GetLength(), 0, dp.pkt->GetAddress());
        if (ret.error_code_ != 0) {
            common::LOG_ERROR(
                "[fault-inject] delayed sendto failed to: %s, len: %d, err: %d",
                dp.pkt->GetAddress().AsString().c_str(), span.GetLength(),
                ret.error_code_);
            common::Metrics::CounterInc(common::MetricsStd::UdpSendErrors);
            return;
        }
        common::Metrics::CounterInc(common::MetricsStd::UdpPacketsTx);
        common::Metrics::CounterInc(common::MetricsStd::UdpBytesTx,
                                    span.GetLength());
    }

    std::mutex mu_;
    std::condition_variable cv_;
    std::queue<DelayedPacket> q_;
    std::once_flag start_once_;
    std::thread worker_;
    bool stop_ = false;
};

}  // namespace

// ============================================================
// Public knobs
// ============================================================

void UdpSender::SetDropPerMillion(uint32_t ratio_per_million) {
    if (ratio_per_million > 1'000'000) {
        ratio_per_million = 1'000'000;
    }
    drop_per_million_.store(ratio_per_million, std::memory_order_relaxed);
}

uint32_t UdpSender::GetDropPerMillion() {
    return drop_per_million_.load(std::memory_order_relaxed);
}

void UdpSender::SetRateLimitBps(uint64_t bytes_per_second) {
    rate_limit_bps_.store(bytes_per_second, std::memory_order_relaxed);
    if (bytes_per_second == 0) {
        Bucket().Reset();
    }
}

uint64_t UdpSender::GetRateLimitBps() {
    return rate_limit_bps_.load(std::memory_order_relaxed);
}

void UdpSender::SetEgressDelayMs(uint32_t delay_ms) {
    egress_delay_ms_.store(delay_ms, std::memory_order_relaxed);
}

uint32_t UdpSender::GetEgressDelayMs() {
    return egress_delay_ms_.load(std::memory_order_relaxed);
}

void UdpSender::ResetFaultInjection() {
    drop_per_million_.store(0, std::memory_order_relaxed);
    rate_limit_bps_.store(0, std::memory_order_relaxed);
    egress_delay_ms_.store(0, std::memory_order_relaxed);
    Bucket().Reset();
    DelayQueue::Instance().Drain();
}

// ============================================================
// Constructors
// ============================================================

UdpSender::UdpSender():
    sock_(-1) {}

UdpSender::UdpSender(int32_t sockfd):
    sock_(sockfd) {}

// ============================================================
// Hot path
// ============================================================

bool UdpSender::Send(std::shared_ptr<NetPacket>& pkt) {
    auto buffer = pkt->GetData();
    auto span = buffer->GetReadableSpan();
    auto sock = pkt->GetSocket() > 0 ? pkt->GetSocket() : sock_;
    if (sock <= 0) {
        common::LOG_ERROR(
            "send packet to: %s, len: %d, sock: %d", pkt->GetAddress().AsString().c_str(), span.GetLength(), sock);
        return false;
    }

    // ---- (1) random loss ----
    // Single relaxed load when disabled. When enabled the synthetic drop is
    // returned as success so callers don't react to it any differently than
    // they would to a real wire-loss event.
    const uint32_t drop_pm = drop_per_million_.load(std::memory_order_relaxed);
    if (drop_pm > 0 && NextRandPerMillion() < drop_pm) {
        common::LOG_DEBUG(
            "[fault-inject] drop egress packet to: %s, len: %d (drop_per_million=%u)",
            pkt->GetAddress().AsString().c_str(), span.GetLength(), drop_pm);
        return true;
    }

    // ---- (2) token-bucket rate limit (tail-drop) ----
    const uint64_t rate = rate_limit_bps_.load(std::memory_order_relaxed);
    if (rate > 0) {
        if (!Bucket().TryConsume(span.GetLength(), rate)) {
            common::LOG_DEBUG(
                "[fault-inject] rate-limit drop to: %s, len: %d (bps=%llu)",
                pkt->GetAddress().AsString().c_str(), span.GetLength(),
                static_cast<unsigned long long>(rate));
            // Tail-drop: same as real congestion drop on a saturated link.
            return true;
        }
    }

    // ---- (3) fixed egress delay ----
    // After loss + rate-limit, surviving packets are either sent immediately
    // (delay == 0, the production path) or handed off to the delay worker.
    const uint32_t delay_ms = egress_delay_ms_.load(std::memory_order_relaxed);
    if (delay_ms > 0) {
        DelayedPacket dp;
        dp.release_at = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(delay_ms);
        dp.pkt = pkt;
        dp.sock = sock;
        DelayQueue::Instance().Enqueue(std::move(dp));
        // Pretend the send succeeded; the worker will actually emit later.
        // Metrics are recorded on emit, not enqueue, so a Drain() doesn't
        // double-count.
        return true;
    }

    // ---- production path ----
    auto ret = common::SendTo(sock, (const char*)span.GetStart(), span.GetLength(), 0, pkt->GetAddress());
    if (ret.error_code_ != 0) {
        common::LOG_ERROR(
            "send packet to: %s, len: %d, err: %d", pkt->GetAddress().AsString().c_str(), span.GetLength(), ret.error_code_);
        common::Metrics::CounterInc(common::MetricsStd::UdpSendErrors);
        return false;
    }
    common::LOG_DEBUG("send packet to: %s, len: %d", pkt->GetAddress().AsString().c_str(), span.GetLength());

    // Metrics: UDP packet sent successfully
    common::Metrics::CounterInc(common::MetricsStd::UdpPacketsTx);
    common::Metrics::CounterInc(common::MetricsStd::UdpBytesTx, span.GetLength());

    return true;
}

}  // namespace quic
}  // namespace quicx
