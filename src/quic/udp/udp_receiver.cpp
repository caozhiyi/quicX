#include <memory>
#include <cstdlib>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif
#include "common/log/log.h"
#include "common/util/time.h"
#include "quic/udp/udp_receiver.h"
#include "quic/common/constants.h"
#include "common/network/io_handle.h"
#include "quic/quicx/global_resource.h"
#include "common/network/if_event_driver.h"

namespace quicx {
namespace quic {

UdpReceiver::UdpReceiver(std::shared_ptr<common::IEventLoop> event_loop):
    event_loop_(event_loop),
    ecn_enabled_(false) {}

UdpReceiver::~UdpReceiver() {}

bool UdpReceiver::AddReceiver(int32_t socket_fd, std::shared_ptr<IPacketReceiver> receiver) {
    common::LOG_DEBUG("UdpReceiver::AddReceiver called: fd=%d, IsInLoopThread=%d", 
                      socket_fd, event_loop_->IsInLoopThread());
    
    if (!event_loop_->IsInLoopThread()) {
        common::LOG_DEBUG("UdpReceiver::AddReceiver: posting to EventLoop thread, fd=%d", socket_fd);
        event_loop_->RunInLoop([this, socket_fd, receiver]() {
            this->AddReceiver(socket_fd, receiver);
        });
        return true;
    }
    
    common::LOG_DEBUG("UdpReceiver::AddReceiver: registering fd=%d in EventLoop", socket_fd);
    receiver_map_[socket_fd] = receiver;
    bool result = event_loop_->RegisterFd(
        socket_fd, common::EventType::ET_READ | common::EventType::ET_ERROR, shared_from_this());
    common::LOG_DEBUG("UdpReceiver::AddReceiver: registration result=%d for fd=%d", result, socket_fd);
    return result;
}

bool UdpReceiver::AddReceiver(const std::string& ip, uint16_t port, std::shared_ptr<IPacketReceiver> receiver) {
    if (!event_loop_->IsInLoopThread()) {
        event_loop_->RunInLoop([this, ip, port, receiver]() {
            this->AddReceiver(ip, port, receiver);
        });
        return true;
    }
    auto ret = common::UdpSocket();
    if (ret.errno_ != 0) {
        common::LOG_ERROR("create udp socket failed. err:%d", ret.errno_);
        return false;
    }

    int32_t socket_fd = ret.return_value_;

    // set noblocking
    auto opt_ret = common::SocketNoblocking(socket_fd);
    if (opt_ret.errno_ != 0) {
        common::LOG_ERROR("udp socket noblocking failed. err:%d", opt_ret.errno_);
        common::Close(socket_fd);
        return false;
    }

    common::Address addr(ip, port);

    if (ecn_enabled_) {
        // enable receiving TOS/TCLASS for ECN via io_handle abstraction
        common::EnableUdpEcn(socket_fd);
    }

    opt_ret = Bind(socket_fd, addr);
    if (opt_ret.errno_ != 0) {
        common::LOG_ERROR("bind address failed. err:%d", opt_ret.errno_);
        common::Close(socket_fd);
        return false;
    }
    if (!event_loop_->RegisterFd(
            socket_fd, common::EventType::ET_READ | common::EventType::ET_ERROR, shared_from_this())) {
        common::LOG_ERROR("register fd failed. fd:%d", socket_fd);
        return false;
    }

    receiver_map_[socket_fd] = receiver;
    return true;
}

bool UdpReceiver::RemoveReceiver(int32_t socket_fd) {
    common::LOG_DEBUG("UdpReceiver::RemoveReceiver called: fd=%d, IsInLoopThread=%d", 
                      socket_fd, event_loop_->IsInLoopThread());
    
    if (!event_loop_->IsInLoopThread()) {
        common::LOG_DEBUG("UdpReceiver::RemoveReceiver: posting to EventLoop thread, fd=%d", socket_fd);
        event_loop_->RunInLoop([this, socket_fd]() {
            this->RemoveReceiver(socket_fd);
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
    event_loop_->RemoveFd(socket_fd);
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
    if (ret.errno_ != 0) {
        if (ret.errno_ == EAGAIN) {
            return;
        }
        common::LOG_ERROR("recv from failed. err:%d", ret.errno_);
        return;
    }

    common::LOG_DEBUG("recv from data from peer. addr: %s, size:%d", peer_addr.AsString().c_str(), ret.return_value_);
    buffer->MoveWritePt(ret.return_value_);
    pkt->SetAddress(std::move(peer_addr));
    pkt->SetSocket(fd);
    pkt->SetTime(common::UTCTimeMsec());
    pkt->SetEcn(ecn_enabled_ ? ecn : 0);

    auto iter = receiver_map_.find(fd);
    if (iter == receiver_map_.end()) {
        common::LOG_ERROR("receiver not found. fd:%d", fd);
        return;
    }

    if (auto receiver = iter->second.lock()) {
        receiver->OnPacket(pkt);
    }
}

void UdpReceiver::OnWrite(uint32_t fd) {
    common::LOG_ERROR("write should not be called. fd:%d", fd);
}

void UdpReceiver::OnError(uint32_t fd) {
    common::LOG_ERROR("something wrong happened. fd:%d", fd);
}

void UdpReceiver::OnClose(uint32_t fd) {
    if (!event_loop_->IsInLoopThread()) {
        event_loop_->RunInLoop([this, fd]() {
            this->OnClose(fd);
        });
        return;
    }
    receiver_map_.erase(fd);
    event_loop_->RemoveFd(fd);
    common::LOG_INFO("udp receiver closed. fd:%d", fd);
}

}  // namespace quic
}  // namespace quicx
