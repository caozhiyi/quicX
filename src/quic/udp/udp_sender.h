#ifndef QUIC_UDP_UDP_SENDER
#define QUIC_UDP_UDP_SENDER

#include <atomic>
#include <cstdint>
#include "quic/udp/if_sender.h"
#include "quic/udp/net_packet.h"

namespace quicx {
namespace quic {

class UdpSender:
    public ISender {
public:
    UdpSender();
    UdpSender(int32_t sockfd);
    ~UdpSender() {}

    bool Send(std::shared_ptr<NetPacket>& pkt) override;

    // sendmmsg-based batch send. See ISender::SendBatch for full contract.
    // Fast path: every packet's destination Address has a cached sockaddr
    // (filled in by a prior Send/SendTo on that Address) and they all share
    // the same socket fd -> a single sendmmsg(2) syscall. When any precondition
    // is not met (cache miss, mixed sockets, fault injection on, batch size
    // exceeds kMaxBatchSize), the implementation transparently falls back to
    // per-packet Send(); cached state established by those Send() calls
    // makes subsequent rounds eligible for the fast path again.
    uint32_t SendBatch(std::vector<std::shared_ptr<NetPacket>>& batch) override;

    int32_t GetSocket() const override { return sock_; }

    // ============================================================
    // Test-only fault injection
    // ============================================================
    //
    // The knobs below are process-wide and only intended for integration
    // tests that want to reproduce realistic WAN-ish conditions (the interop
    // simulator's transfer-loss / mtu rounds in particular) without docker.
    // All of them default to "off"; when off, the fast path adds at most one
    // relaxed atomic load per Send() call, so leaving them in production
    // builds is safe.
    //
    // The three fault types compose in the following order on every Send():
    //   1. Synthetic random loss   (SetDropPerMillion)
    //   2. Token-bucket rate limit (SetRateLimitBps)        -> tail-drop
    //   3. Fixed egress delay      (SetEgressDelayMs)       -> handed off
    //                                                          to a worker
    //
    // Step 2 is intentionally tail-drop rather than queueing-with-backpressure
    // so the burst-loss patterns mirror what a 1Mbps simulator link produces
    // when the sender outruns the link.
    //

    // Drop ratio expressed as drops per 1,000,000 packets:
    //   0       -> never drop (default)
    //   10'000  -> 1% loss
    //   50'000  -> 5% loss
    // Values >= 1'000'000 are clamped to 1'000'000.
    static void SetDropPerMillion(uint32_t ratio_per_million);
    static uint32_t GetDropPerMillion();

    // Token-bucket rate limit on egress (bytes per second). 0 disables the
    // limiter. The bucket capacity is sized at one "burst" worth of bytes
    // (currently 2 * rate / 100 i.e. ~20ms of credit) which is enough to
    // smooth pacing jitter without breaking the 1Mbps cap. When the bucket
    // is empty the packet is DROPPED, mirroring tail-drop on a saturated
    // simulator queue rather than blocking the caller.
    //
    // Typical sim-equivalent values:
    //   1'000'000 / 8     ==  125'000  -> 1 Mbps
    //   10'000'000 / 8    == 1'250'000 -> 10 Mbps
    static void SetRateLimitBps(uint64_t bytes_per_second);
    static uint64_t GetRateLimitBps();

    // Fixed one-way egress delay in milliseconds. 0 disables the delay path
    // and Send() returns to the caller after the actual sendto() (or after a
    // synthetic drop / tail-drop) just like in production.
    //
    // When >0, every Send() that survives loss + rate-limit is handed to a
    // background worker which re-emits the packet at (enqueue_time + delay).
    // The worker is started lazily on the first SetEgressDelayMs(>0) call
    // and is shut down at process exit. Order is FIFO (no jitter / no
    // reordering), matching the interop sim's deterministic delay link.
    //
    // NOTE: the simulator round adds 5ms in EACH direction (~10ms RTT). To
    // mirror that on a single-process loopback test, set 5ms here: traffic
    // from BOTH endpoints flows through the same UdpSender so the delay
    // applies to both directions automatically.
    static void SetEgressDelayMs(uint32_t delay_ms);
    static uint32_t GetEgressDelayMs();

    // Convenience: turn every fault knob off in one call. Call this in
    // TearDown() and in TestBody preludes to make sure no leaked state from
    // a previously-aborted test bleeds into the next.
    static void ResetFaultInjection();

private:
    int32_t sock_;

    // ---- Test-only state. Hot path reads these as relaxed atomics. ----
    static std::atomic<uint32_t> drop_per_million_;
    static std::atomic<uint64_t> rate_limit_bps_;
    static std::atomic<uint32_t> egress_delay_ms_;

    // PERF: single combined flag flipped non-zero whenever ANY of the three
    // knobs above is enabled. The hot path reads this once with relaxed
    // ordering and short-circuits all the per-knob loads + the token-bucket
    // mutex when fault injection is off (the production case). Using a
    // separate variable rather than (a|b|c) avoids 3 dependent loads on
    // the cache line; we accept a tiny race during enable/disable transitions
    // because the knobs are only ever toggled at test setup/teardown.
    static std::atomic<uint32_t> any_fault_enabled_;
};

}
}

#endif
