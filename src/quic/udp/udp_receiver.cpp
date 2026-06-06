#include <cstring>
#include <memory>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <sys/socket.h>
#endif
#include "common/log/log.h"
#include <quicx/common/metrics.h>
#include <quicx/common/metrics_std.h>
#include "common/network/if_event_driver.h"
#include "common/network/io_handle.h"
#include "common/util/time.h"
#include "quic/common/constants.h"
#include "quic/config.h"
#include "quic/quicx/global_resource.h"
#include "quic/udp/udp_receiver.h"

namespace quicx {
namespace quic {

UdpReceiver::UdpReceiver(std::shared_ptr<common::IEventLoop> event_loop):
    event_loop_(event_loop),
    ecn_enabled_(false) {}

UdpReceiver::~UdpReceiver() {
    // Close only those UDP sockets that we created ourselves (via
    // AddReceiver(ip, port, ...)). Sockets registered via AddReceiver(fd, ...)
    // are owned by the caller and must not be closed here (double-close would
    // corrupt fd tables and mis-close a future unrelated fd).
    //
    // Rationale for closing at all: the Linux kernel keeps a UDP port bound as
    // long as any fd referencing it exists, even if the fd is just sitting in
    // a process's descriptor table. Prior to this fix, every server Init that
    // bound a port leaked its listen socket forever, so re-Init on the same
    // port (e.g. repeated benchmark iterations or graceful restart) failed
    // silently with EADDRINUSE and the new server could not receive packets.
    for (int32_t fd : owned_fds_) {
        common::Close(fd);
    }
    owned_fds_.clear();
    receiver_map_.clear();
}

bool UdpReceiver::AddReceiver(int32_t socket_fd, std::shared_ptr<IPacketReceiver> receiver) {
    auto loop = event_loop_.lock();
    if (!loop) return false;
    LOG_DEBUG(
        "UdpReceiver::AddReceiver called: fd=%d, IsInLoopThread=%d", socket_fd, loop->IsInLoopThread());

    if (!loop->IsInLoopThread()) {
        LOG_DEBUG("UdpReceiver::AddReceiver: posting to EventLoop thread, fd=%d", socket_fd);
        auto weak_self = weak_from_this();
        loop->RunInLoop([weak_self, socket_fd, receiver]() {
            auto self = weak_self.lock();
            if (!self) return;
            static_cast<UdpReceiver*>(self.get())->AddReceiver(socket_fd, receiver);
        });
        return true;
    }

    LOG_DEBUG("UdpReceiver::AddReceiver: registering fd=%d in EventLoop", socket_fd);
    receiver_map_[socket_fd] = receiver;
    bool result = loop->RegisterFd(
        socket_fd, common::EventType::ET_READ | common::EventType::ET_ERROR, shared_from_this());
    LOG_DEBUG("UdpReceiver::AddReceiver: registration result=%d for fd=%d", result, socket_fd);
    return result;
}

bool UdpReceiver::AddReceiver(const std::string& ip, uint16_t port, std::shared_ptr<IPacketReceiver> receiver) {
    auto loop = event_loop_.lock();
    if (!loop) return false;
    if (!loop->IsInLoopThread()) {
        auto weak_self = weak_from_this();
        loop->RunInLoop([weak_self, ip, port, receiver]() {
            auto self = weak_self.lock();
            if (!self) return;
            static_cast<UdpReceiver*>(self.get())->AddReceiver(ip, port, receiver);
        });
        return true;
    }
    auto ret = common::UdpSocket();
    if (ret.error_code_ != 0) {
        LOG_ERROR("create udp socket failed. err:%d", ret.error_code_);
        return false;
    }

    int32_t socket_fd = ret.return_value_;

    // set noblocking
    auto opt_ret = common::SocketNoblocking(socket_fd);
    if (opt_ret.error_code_ != 0) {
        LOG_ERROR("udp socket noblocking failed. err:%d", opt_ret.error_code_);
        common::Close(socket_fd);
        return false;
    }

    common::Address addr(ip, port);

    if (ecn_enabled_) {
        // enable receiving TOS/TCLASS for ECN via io_handle abstraction
        common::EnableUdpEcn(socket_fd);
    }

    opt_ret = Bind(socket_fd, addr);
    if (opt_ret.error_code_ != 0) {
        LOG_ERROR("bind address failed. err:%d", opt_ret.error_code_);
        common::Close(socket_fd);
        return false;
    }
    if (!loop->RegisterFd(
            socket_fd, common::EventType::ET_READ | common::EventType::ET_ERROR, shared_from_this())) {
        LOG_ERROR("register fd failed. fd:%d", socket_fd);
        common::Close(socket_fd);
        return false;
    }

    receiver_map_[socket_fd] = receiver;
    owned_fds_.insert(socket_fd);  // we created this fd; we are responsible for closing it
    return true;
}

bool UdpReceiver::RemoveReceiver(int32_t socket_fd) {
    auto loop = event_loop_.lock();
    if (!loop) return false;
    LOG_DEBUG(
        "UdpReceiver::RemoveReceiver called: fd=%d, IsInLoopThread=%d", socket_fd, loop->IsInLoopThread());

    if (!loop->IsInLoopThread()) {
        LOG_DEBUG("UdpReceiver::RemoveReceiver: posting to EventLoop thread, fd=%d", socket_fd);
        auto weak_self = weak_from_this();
        loop->RunInLoop([weak_self, socket_fd]() {
            auto self = weak_self.lock();
            if (!self) return;
            static_cast<UdpReceiver*>(self.get())->RemoveReceiver(socket_fd);
        });
        return true;
    }

    auto iter = receiver_map_.find(socket_fd);
    if (iter == receiver_map_.end()) {
        LOG_DEBUG("UdpReceiver::RemoveReceiver: receiver not found for fd=%d", socket_fd);
        return false;
    }

    LOG_DEBUG("UdpReceiver::RemoveReceiver: removing fd=%d from EventLoop", socket_fd);
    receiver_map_.erase(iter);
    loop->RemoveFd(socket_fd);
    // Only close if we created this fd; caller-owned fds are closed by the caller.
    auto owned_it = owned_fds_.find(socket_fd);
    if (owned_it != owned_fds_.end()) {
        common::Close(socket_fd);
        owned_fds_.erase(owned_it);
    }
    LOG_INFO("UdpReceiver::RemoveReceiver: removed receiver for fd=%d", socket_fd);
    return true;
}

void UdpReceiver::OnRead(uint32_t fd) {
    common::Metrics::CounterInc(common::MetricsStd::DiagUdpOnRead);

    // PERF FIX (loopback throughput on file_transfer):
    //
    // Previously this method recvfrom'd exactly one UDP datagram per call.
    // Combined with the EventLoop loop which runs all fixed_processes_
    // (including Worker::ProcessSend) on every Wait() return, the result
    // was that every incoming data packet caused a full wakeup → recv 1
    // pkt → ProcessSend → emit ACK cycle. wait_ack_packet_numbers_ never
    // got a chance to accumulate beyond 1 entry, so the kAckThreshold=10
    // branch in RecvControl::ShouldSendImmediateAck was almost never hit;
    // ACKs flowed at ~1:1 with data packets, defeating ACK aggregation
    // entirely. Probed numbers (100 MB upload on loopback):
    //
    //   udp_onread ≈ pkts_tx ≈ 22 000 / sec
    //   try_send_iter ≈ 40 000 / sec, active_send ≈ 6 / sec
    //
    // The fix: drain the socket non-blockingly via a single
    // common::RecvFromBatch() call, dispatching each datagram before
    // continuing. This lets ack-eliciting packets pile up in
    // wait_ack_packet_numbers_ within a single wakeup; the very next
    // ProcessSend invocation can then emit one ACK frame covering the
    // whole batch.
    //
    // RecvFromBatch encapsulates the platform-specific syscall:
    //   - Linux: a single recvmmsg(MSG_DONTWAIT) collapses up to N
    //            datagrams into one syscall.
    //   - macOS / Windows: a recvmsg / WSARecvMsg loop with EAGAIN
    //                      early-exit (functional parity, no syscall
    //                      savings).
    // Either way, this method has zero `#ifdef <platform>` and the
    // batching ceiling (kMaxRecvBatch in quic/config.h) is the only
    // knob. 64 was chosen to mirror the send-side kMaxPacketsPerRound
    // and is comfortably above 1 BDP at our cwnd.
    const int max_batch = kMaxRecvBatch;

    // Stack-allocate one entry per slot so we do zero heap traffic on
    // the hot path. kMaxBatch (256) matches the upper bound enforced
    // inside RecvFromBatch. We still clamp `max_batch` defensively in
    // case kMaxRecvBatch is ever raised above 256 in config.h without
    // also enlarging the stack arrays below.
    constexpr int kMaxBatch = 256;
    const int batch_cap = max_batch < kMaxBatch ? max_batch : kMaxBatch;
    int batch = batch_cap;  // may shrink below if buffer prep can't fill all slots

    std::shared_ptr<NetPacket> pkts[kMaxBatch];
    common::RecvBatchEntry entries[kMaxBatch];

    // Wire one freshly-allocated NetPacket into each entry. Buffers come
    // from the thread-local packet allocator so the cost amortizes; on
    // a "0 datagrams" wakeup the unused packets are simply released
    // back to the pool when `pkts[]` goes out of scope.
    //
    // Pool-reuse hazard: a NetPacket returned by Malloc() may have been
    // recycled with its underlying chunk's "floor" still pinned by an
    // outstanding SharedBufferSpan reference. In that case
    // GetWritableSpan() can return far less than kMaxV4PacketSize. If we
    // hand the kernel a `buf_len_` larger than the real free area, a
    // full-MTU datagram will silently overflow into the next chunk in
    // the BlockMemoryPool arena (the chunks are physically contiguous),
    // and the leftover bytes the buffer's own write-pointer is willing
    // to commit (clamped by MoveWritePt) get dispatched as a malformed
    // short-header packet -- this is what produced the "payload too
    // short for header protection sample. payload_len:19" storms.
    //
    // Two-part defense:
    //   (a) Skip / replace any pool-recycled NetPacket whose writable
    //       span cannot hold a full UDP-over-IPv4 datagram. Drop the
    //       reference so the floor can finally retire and grab a fresh
    //       one for this slot.
    //   (b) Always pass the *real* writable length to the kernel, never
    //       a hard-coded MTU constant.
    for (int i = 0; i < batch_cap; ++i) {
        // Cap retries so a permanently-leaking floor in the pool can't
        // wedge OnRead in an infinite loop; if we still don't have a
        // clean buffer after a few tries we surrender this slot. batch
        // will be re-driven on the next readable event anyway.
        constexpr int kMaxRecycleRetries = 8;
        int retries = 0;
        std::shared_ptr<NetPacket> pkt;
        common::BufferSpan span;
        while (retries < kMaxRecycleRetries) {
            pkt = GlobalResource::Instance().GetThreadLocalPacketAllotor()->Malloc();
            span = pkt->GetData()->GetWritableSpan();
            if (span.GetLength() >= kMaxV4PacketSize) {
                break;
            }
            // Recycled NetPacket whose chunk floor is still pinned by
            // an external SharedBufferSpan -- release it (drops one
            // chunk reference, may let the floor retire) and retry.
            pkt.reset();
            ++retries;
        }
        if (!pkt || span.GetLength() < kMaxV4PacketSize) {
            // Could not obtain a clean buffer; shrink the batch to
            // however many slots we have already filled. The remaining
            // datagrams stay queued in the kernel and we'll drain them
            // on the next readable event.
            LOG_WARN("udp recv: pool exhausted after %d retries, batch shrunk from %d to %d",
                     retries, batch_cap, i);
            batch = i;
            break;
        }
        pkts[i] = std::move(pkt);
        entries[i].buf_     = (char*)span.GetStart();
        entries[i].buf_len_ = span.GetLength();
        entries[i].bytes_   = 0;
        entries[i].ecn_     = 0;
    }

    if (batch == 0) {
        // Nothing to receive into; bail before issuing a 0-batch syscall.
        return;
    }

    auto rc = common::RecvFromBatch(fd, entries, batch, ecn_enabled_);
    if (rc.return_value_ <= 0) {
        // 0 datagrams + no error → spurious wakeup / socket already
        // drained; just wait for the next read event. <0 with EAGAIN
        // is already translated to {0,0} inside RecvFromBatch, so any
        // non-zero error_code_ here is a real failure worth logging.
        if (rc.error_code_ != 0
#if !defined(_WIN32)
            && rc.error_code_ != EAGAIN && rc.error_code_ != EWOULDBLOCK
#endif
        ) {
            LOG_ERROR("recv batch failed. err:%d", rc.error_code_);
        }
        return;
    }

    auto recv_iter = receiver_map_.find(fd);
    const bool have_receiver = (recv_iter != receiver_map_.end());
    if (!have_receiver) {
        // Receiver has been removed between event registration and
        // dispatch (e.g. RemoveReceiver raced with this OnRead). Drop
        // every datagram we just pulled off the socket and account for
        // them in metrics; we cannot deliver them anywhere.
        LOG_ERROR("receiver not found. fd:%d", fd);
        for (int i = 0; i < rc.return_value_; ++i) {
            common::Metrics::CounterInc(common::MetricsStd::UdpDroppedPackets);
        }
        return;
    }
    auto receiver_strong = recv_iter->second.lock();

    for (int i = 0; i < rc.return_value_; ++i) {
        auto& pkt = pkts[i];
        const uint32_t bytes = entries[i].bytes_;

        auto buffer = pkt->GetData();
        buffer->MoveWritePt(bytes);
        pkt->SetAddress(std::move(entries[i].peer_addr_));
        pkt->SetSocket(fd);
        pkt->SetTime(common::UTCTimeMsec());
        pkt->SetEcn(ecn_enabled_ ? entries[i].ecn_ : 0);

        common::Metrics::CounterInc(common::MetricsStd::UdpPacketsRx);
        common::Metrics::CounterInc(common::MetricsStd::UdpBytesRx, bytes);

        if (receiver_strong) {
            receiver_strong->OnPacket(pkt);
        } else {
            common::Metrics::CounterInc(common::MetricsStd::UdpDroppedPackets);
        }
    }
}

void UdpReceiver::OnWrite(uint32_t fd) {
    LOG_ERROR("write should not be called. fd:%d", fd);
}

void UdpReceiver::OnError(uint32_t fd) {
    LOG_ERROR("something wrong happened. fd:%d", fd);
}

void UdpReceiver::OnClose(uint32_t fd) {
    auto loop = event_loop_.lock();
    if (!loop) return;
    if (!loop->IsInLoopThread()) {
        auto weak_self = weak_from_this();
        loop->RunInLoop([weak_self, fd]() {
            auto self = weak_self.lock();
            if (!self) return;
            static_cast<UdpReceiver*>(self.get())->OnClose(fd);
        });
        return;
    }
    receiver_map_.erase(fd);
    loop->RemoveFd(fd);
    // Mirror RemoveReceiver(): only close if we own it.
    auto owned_it = owned_fds_.find(fd);
    if (owned_it != owned_fds_.end()) {
        common::Close(fd);
        owned_fds_.erase(owned_it);
    }
    LOG_INFO("udp receiver closed. fd:%d", fd);
}

}  // namespace quic
}  // namespace quicx
