#include "quic/udp/udp_sender.h"
#include "common/log/log.h"
#include <quicx/common/metrics.h>
#include <quicx/common/metrics_std.h>
#include "common/network/io_handle.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
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

// PERF: combined "any fault knob enabled" flag. The hot path reads this with
// relaxed ordering and short-circuits all 3 individual knob loads + the
// token-bucket mutex when fault injection is off (the production case).
// Maintained on every setter / Reset; the small race window during a
// transition is acceptable because knobs are only flipped at test boundaries.
std::atomic<uint32_t> UdpSender::any_fault_enabled_{0};

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
            LOG_ERROR(
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
    // Recompute combined flag: any of the three knobs being non-zero counts.
    const uint32_t any = (ratio_per_million != 0 ||
                          rate_limit_bps_.load(std::memory_order_relaxed) != 0 ||
                          egress_delay_ms_.load(std::memory_order_relaxed) != 0)
                             ? 1u : 0u;
    any_fault_enabled_.store(any, std::memory_order_relaxed);
}

uint32_t UdpSender::GetDropPerMillion() {
    return drop_per_million_.load(std::memory_order_relaxed);
}

void UdpSender::SetRateLimitBps(uint64_t bytes_per_second) {
    rate_limit_bps_.store(bytes_per_second, std::memory_order_relaxed);
    if (bytes_per_second == 0) {
        Bucket().Reset();
    }
    const uint32_t any = (drop_per_million_.load(std::memory_order_relaxed) != 0 ||
                          bytes_per_second != 0 ||
                          egress_delay_ms_.load(std::memory_order_relaxed) != 0)
                             ? 1u : 0u;
    any_fault_enabled_.store(any, std::memory_order_relaxed);
}

uint64_t UdpSender::GetRateLimitBps() {
    return rate_limit_bps_.load(std::memory_order_relaxed);
}

void UdpSender::SetEgressDelayMs(uint32_t delay_ms) {
    egress_delay_ms_.store(delay_ms, std::memory_order_relaxed);
    const uint32_t any = (drop_per_million_.load(std::memory_order_relaxed) != 0 ||
                          rate_limit_bps_.load(std::memory_order_relaxed) != 0 ||
                          delay_ms != 0)
                             ? 1u : 0u;
    any_fault_enabled_.store(any, std::memory_order_relaxed);
}

uint32_t UdpSender::GetEgressDelayMs() {
    return egress_delay_ms_.load(std::memory_order_relaxed);
}

void UdpSender::ResetFaultInjection() {
    drop_per_million_.store(0, std::memory_order_relaxed);
    rate_limit_bps_.store(0, std::memory_order_relaxed);
    egress_delay_ms_.store(0, std::memory_order_relaxed);
    any_fault_enabled_.store(0, std::memory_order_relaxed);
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
    common::Metrics::CounterInc(common::MetricsStd::DiagUdpSendCalls);
    auto buffer = pkt->GetData();
    auto span = buffer->GetReadableSpan();
    auto sock = pkt->GetSocket() > 0 ? pkt->GetSocket() : sock_;
    if (sock <= 0) {
        LOG_ERROR(
            "send packet to: %s, len: %d, sock: %d", pkt->GetAddress().AsString().c_str(), span.GetLength(), sock);
        return false;
    }

    // PERF: production fast path. A single relaxed atomic load is enough to
    // confirm fault injection is off (the production case), allowing us to
    // skip the 3 individual knob loads, the RNG, the token-bucket mutex, and
    // the delay-queue check entirely. This is the dominant path on every
    // real deployment.
    if (any_fault_enabled_.load(std::memory_order_relaxed) == 0) {
        // PERF DIAG: isolate the sendto() syscall cost. On loopback with our
        // observed pkts_tx ~24k/s a one-call-per-packet model puts a strict
        // ceiling around 30-50k pps based on syscall + softirq. If
        // sendto_us mean is in the tens of microseconds, this ceiling is
        // the actual bottleneck and only sendmmsg-style batching can lift
        // it. If it's a few microseconds, the bottleneck is upstream.
        uint64_t sendto_t0 = common::Metrics::NowUs();
        auto ret = common::SendTo(sock, (const char*)span.GetStart(), span.GetLength(), 0, pkt->GetAddress());
        common::Metrics::HistogramObserve(
            common::MetricsStd::DiagSendtoLatencyUs,
            common::Metrics::NowUs() - sendto_t0);
        if (ret.error_code_ != 0) {
            LOG_ERROR(
                "send packet to: %s, len: %d, err: %d", pkt->GetAddress().AsString().c_str(), span.GetLength(), ret.error_code_);
            common::Metrics::CounterInc(common::MetricsStd::UdpSendErrors);
            return false;
        }
        LOG_DEBUG("send packet to: %s, len: %d", pkt->GetAddress().AsString().c_str(), span.GetLength());
        common::Metrics::CounterInc(common::MetricsStd::UdpPacketsTx);
        common::Metrics::CounterInc(common::MetricsStd::UdpBytesTx, span.GetLength());
        common::Metrics::CounterInc(common::MetricsStd::DiagUdpSendOk);
        return true;
    }

    // ---- (1) random loss ----
    // Single relaxed load when disabled. When enabled the synthetic drop is
    // returned as success so callers don't react to it any differently than
    // they would to a real wire-loss event.
    const uint32_t drop_pm = drop_per_million_.load(std::memory_order_relaxed);
    if (drop_pm > 0 && NextRandPerMillion() < drop_pm) {
        LOG_DEBUG(
            "[fault-inject] drop egress packet to: %s, len: %d (drop_per_million=%u)",
            pkt->GetAddress().AsString().c_str(), span.GetLength(), drop_pm);
        return true;
    }

    // ---- (2) token-bucket rate limit (tail-drop) ----
    const uint64_t rate = rate_limit_bps_.load(std::memory_order_relaxed);
    if (rate > 0) {
        if (!Bucket().TryConsume(span.GetLength(), rate)) {
            LOG_DEBUG(
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

    // ---- production path (fault-injection enabled but all knobs let this
    // particular packet through; e.g. delay==0 and rate==0 but drop_pm!=0
    // and the random draw didn't drop). ----
    auto ret = common::SendTo(sock, (const char*)span.GetStart(), span.GetLength(), 0, pkt->GetAddress());
    if (ret.error_code_ != 0) {
        LOG_ERROR(
            "send packet to: %s, len: %d, err: %d", pkt->GetAddress().AsString().c_str(), span.GetLength(), ret.error_code_);
        common::Metrics::CounterInc(common::MetricsStd::UdpSendErrors);
        return false;
    }
    LOG_DEBUG("send packet to: %s, len: %d", pkt->GetAddress().AsString().c_str(), span.GetLength());

    // Metrics: UDP packet sent successfully
    common::Metrics::CounterInc(common::MetricsStd::UdpPacketsTx);
    common::Metrics::CounterInc(common::MetricsStd::UdpBytesTx, span.GetLength());

    return true;
}

// ============================================================
// Batch hot path (sendmmsg + UDP GSO)
// ============================================================
//
// One sendmmsg(2) call replaces up to kMaxBatchSize sendto() calls. The win
// comes from amortizing the userspace<->kernel transition and the per-call
// UDP socket lock acquisition over the whole batch. On a 50MB loopback
// file_transfer this is the lever that lifts the per-worker syscall rate
// from ~1 syscall/packet to ~1 syscall per drain round (kMaxPacketsPerRound
// in worker.cpp, currently 128).
//
// On top of sendmmsg, this implementation **opportunistically uses UDP GSO
// (Linux UDP_SEGMENT, kernel 4.18+)**: when a contiguous prefix of the batch
// shares the same destination Address AND the same packet length (except the
// trailing packet which may be shorter), we coalesce that run into a single
// sendmsg + UDP_SEGMENT cmsg. The kernel slices the payload into N datagrams
// internally, traversing the protocol stack only once. Compared with sendmmsg
// (which still walks the stack N times) this is a ~2-5x CPU reduction on the
// send path for QUIC-style "many same-sized packets to one peer" traffic —
// which is exactly what BuildDataPacket emits during a steady-state stream.
//
// The GSO path is gated behind a process-wide static flag that is permanently
// disabled on the first ENOTSUP/EINVAL/EIO so a kernel/path that doesn't
// support UDP_SEGMENT (older kernels, certain network namespaces, macOS,
// Windows) silently falls back to sendmmsg without re-paying probing cost.
//
// FAST PATH PRECONDITIONS (all must hold; otherwise we fall back to per-packet
// Send so semantics stay identical):
//   1. Fault injection is OFF. Drop / rate-limit / delay knobs need per-packet
//      decisions, so we degrade to Send() to keep their behavior intact.
//   2. The batch is non-empty and within kMaxBatchSize.
//   3. Every packet's destination Address already has a cached binary
//      sockaddr (filled in by a prior Send/SendTo on that Address). The
//      first Send() per Address populates the cache, so a brand-new
//      connection's first round naturally falls back here, and every
//      subsequent round on the same Address takes the fast path.
//   4. Every packet shares the same socket fd and the same address-family
//      cache slot. Mixed-socket batches are rare (only matters during path
//      migration) and not worth the complexity of per-fd grouping at this
//      layer.
//
// If any of (3)/(4) fails for *any* packet, we fall back to per-packet Send
// for the whole batch rather than partially batching. This is intentional:
// (a) the very first round on a connection populates caches as a side
//     effect, so we'd just make the second round eligible anyway;
// (b) avoids subtle ordering bugs when only some packets in the batch
//     actually fly via sendmmsg.
//
namespace {
// Process-wide flag flipped to true on first GSO attempt that returns a
// "kernel/path doesn't accept UDP_SEGMENT" errno. Once set, subsequent
// SendBatch calls skip the GSO probing and go straight to sendmmsg.
// Relaxed atomic is sufficient: a brief race window where two threads
// each do one final "doomed" GSO send is harmless (each just returns
// EINVAL once and sets the flag).
std::atomic<bool> g_gso_unsupported{false};

// Linux UDP_MAX_SEGMENTS hard cap (drivers reject larger). 64 is the
// historic kernel limit and is the safe ceiling across 4.18..6.x kernels.
constexpr size_t kGsoMaxSegments = 64;

// Per-thread scratch buffer for coalescing GSO segments. Sized for the
// worst case kGsoMaxSegments * MTU (~1500). thread_local so concurrent
// workers don't share / lock; stays alive for the thread's lifetime so
// we don't pay an allocation per batch.
constexpr size_t kGsoScratchBytes = kGsoMaxSegments * 2048;  // 128KB headroom
}  // namespace

uint32_t UdpSender::SendBatch(std::vector<std::shared_ptr<NetPacket>>& batch) {
    const size_t n = batch.size();
    if (n == 0) {
        return 0;
    }
    common::Metrics::CounterInc(common::MetricsStd::DiagUdpSendBatchCalls);

    // Fault-injection enabled -> degrade to per-packet so drop/rate/delay
    // semantics remain bit-for-bit identical to the non-batch path.
    if (any_fault_enabled_.load(std::memory_order_relaxed) != 0) {
        uint32_t ok = 0;
        for (auto& p : batch) {
            if (Send(p)) {
                ok++;
            }
        }
        return ok;
    }

    // Cap the batch size at the syscall API's natural limit. UIO_MAXIOV is
    // 1024 on Linux but 128 already amortizes ~99% of the per-syscall cost
    // (Worker drains <=128 per round anyway), and keeping the on-stack
    // arrays small keeps this function's stack footprint bounded.
    constexpr size_t kMaxBatchSize = 128;
    const size_t batch_n = n > kMaxBatchSize ? kMaxBatchSize : n;

    // Probe the first packet for the canonical socket fd + cached family.
    // Every other packet must agree, otherwise we degrade to per-packet
    // Send (see precondition #4 in the header comment).
    auto& first = batch[0];
    const int32_t sock0 = first->GetSocket() > 0 ? first->GetSocket() : sock_;
    if (sock0 <= 0) {
        // No usable socket; let Send() emit a structured error per packet.
        uint32_t ok = 0;
        for (auto& p : batch) {
            if (Send(p)) {
                ok++;
            }
        }
        return ok;
    }

    // Determine which family slot every Address must have cached. We don't
    // know the socket's domain at this layer, so we infer from whichever
    // slot is filled on the first packet. Production traffic uses one
    // family per socket so this is stable for the life of the connection.
    socklen_t probe_len = 0;
    int probe_family = AF_INET;
    if (!first->GetAddress().GetCachedSockaddr(AF_INET, probe_len)) {
        probe_len = 0;
        if (first->GetAddress().GetCachedSockaddr(AF_INET6, probe_len)) {
            probe_family = AF_INET6;
        } else {
            // Cache miss on the very first packet -> degrade to Send() for
            // the whole batch. As a side effect every Send() populates its
            // Address's cache, so the next SendBatch round is fast-path
            // eligible. This is the natural warm-up path for a new
            // connection.
            uint32_t ok = 0;
            for (auto& p : batch) {
                if (Send(p)) {
                    ok++;
                }
            }
            return ok;
        }
    }

    // ---- assemble mmsghdr / iovec arrays on the stack ----
    common::MMsghdr msgs[kMaxBatchSize];
    common::Iovec   iovs[kMaxBatchSize];

    size_t prepared = 0;
    for (; prepared < batch_n; prepared++) {
        auto& pkt = batch[prepared];
        const int32_t s = pkt->GetSocket() > 0 ? pkt->GetSocket() : sock_;
        if (s != sock0) {
            // Mixed sockets in a single batch -> degrade. Bail out before
            // any sendmmsg so ordering stays simple.
            break;
        }

        socklen_t cached_len = 0;
        const struct sockaddr* cached =
            pkt->GetAddress().GetCachedSockaddr(probe_family, cached_len);
        if (!cached) {
            break;  // any cache miss -> degrade
        }

        auto buffer = pkt->GetData();
        if (!buffer) {
            break;
        }
        auto span = buffer->GetReadableSpan();

        iovs[prepared].iov_base_ = const_cast<uint8_t*>(span.GetStart());
        iovs[prepared].iov_len_  = span.GetLength();

        common::Msghdr& hdr = msgs[prepared].msg_hdr_;
        hdr.msg_name_       = const_cast<struct sockaddr*>(cached);
        hdr.msg_namelen_    = cached_len;
        hdr.msg_iov_        = &iovs[prepared];
        hdr.msg_iovlen_     = 1;
        hdr.msg_control_    = nullptr;
        hdr.msg_controllen_ = 0;
        hdr.msg_flags_      = 0;
        msgs[prepared].msg_len_ = 0;
    }

    if (prepared != batch_n) {
        // Some precondition failed mid-batch (cache miss or mixed socket).
        // Degrade the whole batch to per-packet Send to avoid partial-send
        // ordering hazards.
        uint32_t ok = 0;
        for (auto& p : batch) {
            if (Send(p)) {
                ok++;
            }
        }
        return ok;
    }

    // ---- (GSO fast-fast-path) ----
    //
    // Try to find a contiguous prefix [0, gso_run) of the prepared batch
    // where every packet has:
    //   - the same destination sockaddr pointer (i.e. same Address
    //     instance — ptr equality is the cheapest test and is always
    //     true for steady-state same-connection traffic since the cache
    //     stores into Address::sockaddr_storage_ inside the Address
    //     object, and a single connection sends to a single Address);
    //   - the same packet length, except possibly the LAST one which may
    //     be shorter (UDP_SEGMENT allows the trailing segment to be
    //     short).
    //
    // If gso_run >= 2 we coalesce that run into one sendmsg+UDP_SEGMENT.
    // The remaining [gso_run, batch_n) tail is sent via the normal
    // sendmmsg path right after, preserving FIFO order.
    //
    // Why ptr equality on msg_name_ rather than sockaddr-bytes equality?
    // Because every prepared[i].msg_hdr_.msg_name_ already points to the
    // *cached* sockaddr inside its Address instance. Two packets share
    // that pointer iff they share an Address instance — exactly the
    // condition we need. This is O(1) per pair and zero-allocation.
    bool used_gso = false;
    size_t gso_sent_pkts = 0;
    if (!g_gso_unsupported.load(std::memory_order_relaxed) && batch_n >= 2) {
        const struct sockaddr* ref_addr =
            static_cast<const struct sockaddr*>(msgs[0].msg_hdr_.msg_name_);
        const uint32_t ref_alen = msgs[0].msg_hdr_.msg_namelen_;
        const size_t ref_len = iovs[0].iov_len_;
        size_t gso_run = 1;
        for (; gso_run < batch_n && gso_run < kGsoMaxSegments; ++gso_run) {
            // Compare sockaddr by content rather than pointer: each
            // NetPacket owns its own Address (and hence its own cached
            // sockaddr storage), so pointer equality is essentially
            // never true even for the same peer. Content equality on the
            // already-decoded binary sockaddr is cheap (16-28 bytes).
            if (msgs[gso_run].msg_hdr_.msg_namelen_ != ref_alen ||
                memcmp(msgs[gso_run].msg_hdr_.msg_name_, ref_addr, ref_alen) != 0) {
                break;
            }
            const size_t this_len = iovs[gso_run].iov_len_;
            if (this_len > ref_len) break;          // trailing must be <= ref
            if (this_len < ref_len) {
                // Allow ONLY if it's the last packet of the run.
                ++gso_run;
                break;
            }
        }
        if (gso_run >= 2) {
            // Coalesce gso_run packets into one contiguous payload. We
            // pay one memcpy per packet (~1200 bytes each), but save
            // gso_run-1 trips through the kernel UDP stack — net win is
            // small on loopback (where syscall+stack are already cheap)
            // but ~2-3x on real NICs that support hardware UDP_GSO.
            thread_local std::vector<uint8_t> scratch(kGsoScratchBytes);
            uint8_t* dst = scratch.data();
            size_t total = 0;
            for (size_t i = 0; i < gso_run; ++i) {
                total += iovs[i].iov_len_;
            }
            if (total <= scratch.size() && total <= 65000) {
                for (size_t i = 0; i < gso_run; ++i) {
                    memcpy(dst, iovs[i].iov_base_, iovs[i].iov_len_);
                    dst += iovs[i].iov_len_;
                }

                const uint64_t gt0 = common::Metrics::NowUs();
                auto gret = common::SendMsgGso(
                    sock0,
                    reinterpret_cast<const char*>(scratch.data()),
                    static_cast<uint32_t>(total),
                    static_cast<uint16_t>(ref_len),
                    batch[0]->GetAddress());
                const uint64_t gdt = common::Metrics::NowUs() - gt0;

                if (gret.return_value_ >= 0) {
                    for (size_t i = 0; i < gso_run; ++i) {
                        common::Metrics::CounterInc(common::MetricsStd::UdpPacketsTx);
                        common::Metrics::CounterInc(common::MetricsStd::UdpBytesTx,
                                                    iovs[i].iov_len_);
                    }
                    common::Metrics::HistogramObserve(
                        common::MetricsStd::DiagSendtoLatencyUs, gdt / gso_run);
                    common::Metrics::CounterInc(common::MetricsStd::DiagUdpSendBatchOk);
                    used_gso = true;
                    gso_sent_pkts = gso_run;
                } else {
                    const int e = gret.error_code_;
                    if (e == EINVAL || e == ENOTSUP || e == EIO
#ifdef ENOPROTOOPT
                        || e == ENOPROTOOPT
#endif
                    ) {
                        g_gso_unsupported.store(true, std::memory_order_relaxed);
                        LOG_WARN("UDP GSO unsupported (errno=%d), "
                                 "falling back to sendmmsg permanently", e);
                    }
                    // Either way, drop through to sendmmsg below.
                }
            }
        }
    }

    // If GSO sent the leading run, shrink the sendmmsg call to the
    // remaining tail. This is just a base-pointer + count adjustment;
    // no array copy needed because msgs/iovs are still in scope.
    common::MMsghdr*       mm_send  = msgs;
    const common::Iovec*   iov_send = iovs;
    size_t mm_count = batch_n;
    if (used_gso) {
        mm_send  += gso_sent_pkts;
        iov_send += gso_sent_pkts;
        mm_count -= gso_sent_pkts;
    }
    (void)iov_send;  // only used inside the loop above; suppress unused warning

    if (mm_count == 0) {
        // Whole batch went via GSO. Done.
        return static_cast<uint32_t>(gso_sent_pkts);
    }

    // ---- one sendmmsg(2) ----
    const uint64_t t0 = common::Metrics::NowUs();
    auto ret = common::SendmMsg(sock0, mm_send, static_cast<uint32_t>(mm_count), 0);
    const uint64_t dt = common::Metrics::NowUs() - t0;
    // Report a per-datagram latency sample (mean over the batch). This keeps
    // the kSendtoLat distribution comparable to the pre-batching baseline so
    // perf experiments don't need a separate phase.
    if (mm_count > 0) {
        common::Metrics::HistogramObserve(
            common::MetricsStd::DiagSendtoLatencyUs,
            dt / mm_count);
    }

    if (ret.return_value_ < 0) {
        // Whole-batch sendmmsg failure (e.g. EINTR before any packet was
        // queued). Fall back to per-packet Send so the existing single-
        // packet error handling kicks in (logging, metrics, etc.).
        LOG_ERROR("sendmmsg failed: vlen=%zu, err=%d -> degrade to Send()",
                  mm_count, ret.error_code_);
        // Account already-sent GSO packets, then resend just the tail.
        uint32_t ok = static_cast<uint32_t>(gso_sent_pkts);
        for (size_t i = gso_sent_pkts; i < batch.size(); ++i) {
            if (Send(batch[i])) {
                ok++;
            }
        }
        return ok;
    }

    const uint32_t sent = static_cast<uint32_t>(ret.return_value_);
    // Account metrics for the packets the kernel accepted. Bytes use
    // msg_len_ when populated (Linux sendmmsg(2) fills it on success); fall
    // back to the iov length for the macOS sendmsg-loop emulation that
    // doesn't update msg_len_.
    for (uint32_t k = 0; k < sent; k++) {
        const uint32_t bytes =
            mm_send[k].msg_len_ ? mm_send[k].msg_len_
                                : static_cast<uint32_t>(mm_send[k].msg_hdr_.msg_iov_->iov_len_);
        common::Metrics::CounterInc(common::MetricsStd::UdpPacketsTx);
        common::Metrics::CounterInc(common::MetricsStd::UdpBytesTx, bytes);
    }
    if (sent < mm_count) {
        // Short-write: trailing packets were not sent. Rather than buffering
        // them across drain rounds (which would invert FIFO order with the
        // next round's traffic), drop them — QUIC's loss detection will
        // retransmit. Log so unexpected losses are visible.
        LOG_WARN("sendmmsg short-write: %u/%zu, dropped %zu",
                 sent, mm_count, mm_count - sent);
        common::Metrics::CounterInc(common::MetricsStd::UdpSendErrors,
                                    mm_count - sent);
    }
    if (sent > 0) {
        common::Metrics::CounterInc(common::MetricsStd::DiagUdpSendBatchOk);
    }
    return sent + static_cast<uint32_t>(gso_sent_pkts);
}

}  // namespace quic
}  // namespace quicx
