#include <atomic>
#include <cstdlib>
#include <cstring>
#include <quicx/common/metrics.h>

#include "common/log/log.h"
#include "common/metrics/metrics_std.h"
#include "common/network/io_handle.h"

#include "quic/config.h"
#include "quic/udp/udp_sender.h"

namespace quicx {
namespace quic {

// ============================================================
// Constructors
// ============================================================

UdpSender::UdpSender():
    sock_(-1, 0) {}

UdpSender::UdpSender(int32_t sockfd):
    sock_(sockfd, 0) {}

UdpSender::UdpSender(common::SocketHandle sock):
    sock_(sock) {}

// ============================================================
// Hot path
// ============================================================

bool UdpSender::Send(std::shared_ptr<NetPacket>& pkt) {
    Metrics::CounterInc(common::MetricsStd::DiagUdpSendCalls);
    auto buffer = pkt->GetData();
    auto span = buffer->GetReadableSpan();
    const common::SocketHandle sock = pkt->GetSocket().fd > 0 ? pkt->GetSocket() : sock_;
    if (sock.fd <= 0) {
        LOG_ERROR(
            "send packet to: %s, len: %d, sock: %d", pkt->GetAddress().AsString().c_str(), span.GetLength(), sock.fd);
        return false;
    }

    // PERF DIAG: isolate the sendto() syscall cost. On loopback with our
    // observed pkts_tx ~24k/s a one-call-per-packet model puts a strict
    // ceiling around 30-50k pps based on syscall + softirq. If
    // sendto_us mean is in the tens of microseconds, this ceiling is
    // the actual bottleneck and only sendmmsg-style batching can lift
    // it. If it's a few microseconds, the bottleneck is upstream.
    uint64_t sendto_t0 = Metrics::NowUs();
    auto ret = common::SendTo(sock, (const char*)span.GetStart(), span.GetLength(), 0, pkt->GetAddress());
    Metrics::HistogramObserve(common::MetricsStd::DiagSendtoLatencyUs, Metrics::NowUs() - sendto_t0);
    if (ret.error_code_ != 0) {
        LOG_ERROR("send packet to: %s, len: %d, err: %d", pkt->GetAddress().AsString().c_str(), span.GetLength(),
            ret.error_code_);
        Metrics::CounterInc(common::MetricsStd::UdpSendErrors);
        return false;
    }
    LOG_DEBUG("send packet to: %s, len: %d", pkt->GetAddress().AsString().c_str(), span.GetLength());
    Metrics::CounterInc(common::MetricsStd::UdpPacketsTx);
    Metrics::CounterInc(common::MetricsStd::UdpBytesTx, span.GetLength());
    Metrics::CounterInc(common::MetricsStd::DiagUdpSendOk);
    return true;
}

// ============================================================
// Batch hot path (sendmmsg + UDP GSO)
// ============================================================
//
// One sendmmsg(2) call replaces up to kMaxPacketsPerRound sendto() calls. The win
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
//   1. The batch is non-empty and within kMaxPacketsPerRound.
//   2. Every packet's destination Address already has a cached binary
//      sockaddr (filled in by a prior Send/SendTo on that Address). The
//      first Send() per Address populates the cache, so a brand-new
//      connection's first round naturally falls back here, and every
//      subsequent round on the same Address takes the fast path.
//   3. Every packet shares the same socket fd and the same address-family
//      cache slot. Mixed-socket batches are rare (only matters during path
//      migration) and not worth the complexity of per-fd grouping at this
//      layer.
//
// If any of (2)/(3) fails for *any* packet, we fall back to per-packet Send
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
// Now defined in quic/config.h as kGsoMaxSegments.

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
    Metrics::CounterInc(common::MetricsStd::DiagUdpSendBatchCalls);

    // Cap the batch size at the syscall API's natural limit. UIO_MAXIOV is
    // 1024 on Linux but 128 already amortizes ~99% of the per-syscall cost
    // (Worker drains <=128 per round anyway), and keeping the on-stack
    // arrays small keeps this function's stack footprint bounded.
    const size_t batch_n = n > kMaxPacketsPerRound ? kMaxPacketsPerRound : n;

    // Probe the first packet for the canonical socket handle (fd + family).
    // Every other packet must agree, otherwise we degrade to per-packet
    // Send (see precondition #3 in the header comment).
    auto& first = batch[0];
    const common::SocketHandle sock0 = first->GetSocket().fd > 0 ? first->GetSocket() : sock_;
    if (sock0.fd <= 0) {
        // No usable socket; let Send() emit a structured error per packet.
        uint32_t ok = 0;
        for (auto& p : batch) {
            if (Send(p)) {
                ok++;
            }
        }
        return ok;
    }

    // Determine which family slot every Address must have cached. The
    // cache family MUST match the socket's actual family — on macOS
    // (and Linux dual-stack), passing an AF_INET sockaddr to an
    // AF_INET6 socket returns EINVAL from sendmsg, which historically
    // caused every batch to fall back to per-packet Send(), making
    // sendmmsg a no-op on the entire hot path. The handle carries the
    // family from creation time (free); only a foreign fd with family 0
    // pays the ResolveSocketFamily syscall fallback.
    int32_t sock_family = sock0.family;
    if (sock_family != AF_INET6 && sock_family != AF_INET) {
        sock_family = common::ResolveSocketFamily(sock0.fd);
    }
    socklen_t probe_len = 0;
    int probe_family = (sock_family == AF_INET6) ? AF_INET6 : AF_INET;
    if (!first->GetAddress().GetCachedSockaddr(probe_family, probe_len)) {
        // Fall back to the other family slot in case EnsureSockaddrCache
        // only populated one side (e.g. v4 cache when the addr's IP is
        // textually IPv4 and family resolution returned AF_UNSPEC).
        const int alt_family = (probe_family == AF_INET) ? AF_INET6 : AF_INET;
        probe_len = 0;
        if (first->GetAddress().GetCachedSockaddr(alt_family, probe_len)) {
            probe_family = alt_family;
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
    common::MMsghdr msgs[kMaxPacketsPerRound];
    common::Iovec iovs[kMaxPacketsPerRound];

    size_t prepared = 0;
    for (; prepared < batch_n; prepared++) {
        auto& pkt = batch[prepared];
        const common::SocketHandle s = pkt->GetSocket().fd > 0 ? pkt->GetSocket() : sock_;
        if (s.fd != sock0.fd) {
            // Mixed sockets in a single batch -> degrade. Bail out before
            // any sendmmsg so ordering stays simple.
            break;
        }

        socklen_t cached_len = 0;
        const struct sockaddr* cached = pkt->GetAddress().GetCachedSockaddr(probe_family, cached_len);
        if (!cached) {
            break;  // any cache miss -> degrade
        }

        auto buffer = pkt->GetData();
        if (!buffer) {
            break;
        }
        auto span = buffer->GetReadableSpan();

        iovs[prepared].iov_base_ = const_cast<uint8_t*>(span.GetStart());
        iovs[prepared].iov_len_ = span.GetLength();

        common::Msghdr& hdr = msgs[prepared].msg_hdr_;
        hdr.msg_name_ = const_cast<struct sockaddr*>(cached);
        hdr.msg_namelen_ = cached_len;
        hdr.msg_iov_ = &iovs[prepared];
        hdr.msg_iovlen_ = 1;
        hdr.msg_control_ = nullptr;
        hdr.msg_controllen_ = 0;
        hdr.msg_flags_ = 0;
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
        const struct sockaddr* ref_addr = static_cast<const struct sockaddr*>(msgs[0].msg_hdr_.msg_name_);
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
            if (this_len > ref_len) break;  // trailing must be <= ref
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

                const uint64_t gt0 = Metrics::NowUs();
                auto gret = common::SendMsgGso(sock0, reinterpret_cast<const char*>(scratch.data()),
                    static_cast<uint32_t>(total), static_cast<uint16_t>(ref_len), batch[0]->GetAddress());
                const uint64_t gdt = Metrics::NowUs() - gt0;

                if (gret.return_value_ >= 0) {
                    for (size_t i = 0; i < gso_run; ++i) {
                        Metrics::CounterInc(common::MetricsStd::UdpPacketsTx);
                        Metrics::CounterInc(common::MetricsStd::UdpBytesTx, iovs[i].iov_len_);
                    }
                    Metrics::HistogramObserve(common::MetricsStd::DiagSendtoLatencyUs, gdt / gso_run);
                    Metrics::CounterInc(common::MetricsStd::DiagUdpSendBatchOk);
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
                        LOG_WARN(
                            "UDP GSO unsupported (errno=%d), "
                            "falling back to sendmmsg permanently",
                            e);
                    }
                    // Either way, drop through to sendmmsg below.
                }
            }
        }
    }

    // If GSO sent the leading run, shrink the sendmmsg call to the
    // remaining tail. This is just a base-pointer + count adjustment;
    // no array copy needed because msgs/iovs are still in scope.
    common::MMsghdr* mm_send = msgs;
    const common::Iovec* iov_send = iovs;
    size_t mm_count = batch_n;
    if (used_gso) {
        mm_send += gso_sent_pkts;
        iov_send += gso_sent_pkts;
        mm_count -= gso_sent_pkts;
    }
    (void)iov_send;  // only used inside the loop above; suppress unused warning

    if (mm_count == 0) {
        // Whole batch went via GSO. Done.
        return static_cast<uint32_t>(gso_sent_pkts);
    }

    // ---- one sendmmsg(2) ----
    const uint64_t t0 = Metrics::NowUs();
    auto ret = common::SendmMsg(sock0.fd, mm_send, static_cast<uint32_t>(mm_count), 0);
    const uint64_t dt = Metrics::NowUs() - t0;
    // Report a per-datagram latency sample (mean over the batch). This keeps
    // the kSendtoLat distribution comparable to the pre-batching baseline so
    // perf experiments don't need a separate phase.
    if (mm_count > 0) {
        Metrics::HistogramObserve(common::MetricsStd::DiagSendtoLatencyUs, dt / mm_count);
    }

    if (ret.return_value_ < 0) {
        // Whole-batch sendmmsg failure (e.g. EINTR before any packet was
        // queued). Fall back to per-packet Send so the existing single-
        // packet error handling kicks in (logging, metrics, etc.).
        LOG_ERROR("sendmmsg failed: vlen=%zu, err=%d -> degrade to Send()", mm_count, ret.error_code_);
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
            mm_send[k].msg_len_ ? mm_send[k].msg_len_ : static_cast<uint32_t>(mm_send[k].msg_hdr_.msg_iov_->iov_len_);
        Metrics::CounterInc(common::MetricsStd::UdpPacketsTx);
        Metrics::CounterInc(common::MetricsStd::UdpBytesTx, bytes);
    }
    if (sent < mm_count) {
        // Short-write: trailing packets were not sent. Rather than buffering
        // them across drain rounds (which would invert FIFO order with the
        // next round's traffic), drop them — QUIC's loss detection will
        // retransmit. Log so unexpected losses are visible.
        LOG_WARN("sendmmsg short-write: %u/%zu, dropped %zu", sent, mm_count, mm_count - sent);
        Metrics::CounterInc(common::MetricsStd::UdpSendErrors, mm_count - sent);
    }
    if (sent > 0) {
        Metrics::CounterInc(common::MetricsStd::DiagUdpSendBatchOk);
    }
    return sent + static_cast<uint32_t>(gso_sent_pkts);
}

}  // namespace quic
}  // namespace quicx
