#include <cstdlib>
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
    common::LOG_DEBUG(
        "UdpReceiver::AddReceiver called: fd=%d, IsInLoopThread=%d", socket_fd, loop->IsInLoopThread());

    if (!loop->IsInLoopThread()) {
        common::LOG_DEBUG("UdpReceiver::AddReceiver: posting to EventLoop thread, fd=%d", socket_fd);
        auto weak_self = weak_from_this();
        loop->RunInLoop([weak_self, socket_fd, receiver]() {
            auto self = weak_self.lock();
            if (!self) return;
            static_cast<UdpReceiver*>(self.get())->AddReceiver(socket_fd, receiver);
        });
        return true;
    }

    common::LOG_DEBUG("UdpReceiver::AddReceiver: registering fd=%d in EventLoop", socket_fd);
    receiver_map_[socket_fd] = receiver;
    bool result = loop->RegisterFd(
        socket_fd, common::EventType::ET_READ | common::EventType::ET_ERROR, shared_from_this());
    common::LOG_DEBUG("UdpReceiver::AddReceiver: registration result=%d for fd=%d", result, socket_fd);
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
        common::LOG_ERROR("create udp socket failed. err:%d", ret.error_code_);
        return false;
    }

    int32_t socket_fd = ret.return_value_;

    // set noblocking
    auto opt_ret = common::SocketNoblocking(socket_fd);
    if (opt_ret.error_code_ != 0) {
        common::LOG_ERROR("udp socket noblocking failed. err:%d", opt_ret.error_code_);
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
        common::LOG_ERROR("bind address failed. err:%d", opt_ret.error_code_);
        common::Close(socket_fd);
        return false;
    }
    if (!loop->RegisterFd(
            socket_fd, common::EventType::ET_READ | common::EventType::ET_ERROR, shared_from_this())) {
        common::LOG_ERROR("register fd failed. fd:%d", socket_fd);
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
    common::LOG_DEBUG(
        "UdpReceiver::RemoveReceiver called: fd=%d, IsInLoopThread=%d", socket_fd, loop->IsInLoopThread());

    if (!loop->IsInLoopThread()) {
        common::LOG_DEBUG("UdpReceiver::RemoveReceiver: posting to EventLoop thread, fd=%d", socket_fd);
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
        common::LOG_DEBUG("UdpReceiver::RemoveReceiver: receiver not found for fd=%d", socket_fd);
        return false;
    }

    common::LOG_DEBUG("UdpReceiver::RemoveReceiver: removing fd=%d from EventLoop", socket_fd);
    receiver_map_.erase(iter);
    loop->RemoveFd(socket_fd);
    // Only close if we created this fd; caller-owned fds are closed by the caller.
    auto owned_it = owned_fds_.find(socket_fd);
    if (owned_it != owned_fds_.end()) {
        common::Close(socket_fd);
        owned_fds_.erase(owned_it);
    }
    common::LOG_INFO("UdpReceiver::RemoveReceiver: removed receiver for fd=%d", socket_fd);
    return true;
}

void UdpReceiver::OnRead(uint32_t fd) {
    std::shared_ptr<NetPacket> pkt = GlobalResource::Instance().GetThreadLocalPacketAllotor()->Malloc();

    auto buffer = pkt->GetData();
    auto span = buffer->GetWritableSpan();

    // Use platform abstraction to capture ECN and peer address
    common::Address peer_addr;
    uint8_t ecn = 0;
    auto ret = common::RecvFromWithEcn(fd, (char*)span.GetStart(), kMaxV4PacketSize, 0, peer_addr, ecn);
    if (ret.error_code_ != 0) {
        if (ret.error_code_ == EAGAIN) {
            return;
        }
        common::LOG_ERROR("recv from failed. err:%d", ret.error_code_);
        return;
    }

    common::LOG_DEBUG("recv from data from peer. addr: %s, size:%d", peer_addr.AsString().c_str(), ret.return_value_);
    buffer->MoveWritePt(ret.return_value_);
    pkt->SetAddress(std::move(peer_addr));
    pkt->SetSocket(fd);
    pkt->SetTime(common::UTCTimeMsec());
    pkt->SetEcn(ecn_enabled_ ? ecn : 0);

    // Metrics: UDP packet received successfully
    common::Metrics::CounterInc(common::MetricsStd::UdpPacketsRx);
    common::Metrics::CounterInc(common::MetricsStd::UdpBytesRx, ret.return_value_);

    auto iter = receiver_map_.find(fd);
    if (iter == receiver_map_.end()) {
        common::LOG_ERROR("receiver not found. fd:%d", fd);
        common::Metrics::CounterInc(common::MetricsStd::UdpDroppedPackets);
        return;
    }

    if (auto receiver = iter->second.lock()) {
        receiver->OnPacket(pkt);
    } else {
        common::Metrics::CounterInc(common::MetricsStd::UdpDroppedPackets);
    }
}

void UdpReceiver::OnWrite(uint32_t fd) {
    common::LOG_ERROR("write should not be called. fd:%d", fd);
}

void UdpReceiver::OnError(uint32_t fd) {
    common::LOG_ERROR("something wrong happened. fd:%d", fd);
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
    common::LOG_INFO("udp receiver closed. fd:%d", fd);
}

}  // namespace quic
}  // namespace quicx
