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
#include "common/network/if_event_driver.h"


namespace quicx {
namespace quic {

UdpReceiver::UdpReceiver(std::shared_ptr<common::IEventLoop> event_loop):
    event_loop_(event_loop) {
    packet_allotor_ = IPacketAllotor::MakePacketAllotor(IPacketAllotor::PacketAllotorType::POOL);
}

UdpReceiver::~UdpReceiver() {

}

void UdpReceiver::AddReceiver(int32_t socket_fd, std::shared_ptr<IPacketReceiver> receiver) {
    // set noblocking
    auto opt_ret = common::SocketNoblocking(socket_fd);
    if (opt_ret.errno_ != 0) {
        common::LOG_ERROR("udp socket noblocking failed. err:%d", opt_ret.errno_);
        abort();
    }

    if (auto event_loop = event_loop_.lock()) {
        event_loop->RegisterFd(socket_fd, common::EventType::ET_READ, shared_from_this());
    }
    receiver_map_[socket_fd] = receiver;
}

int32_t UdpReceiver::AddReceiver(const std::string& ip, uint16_t port, std::shared_ptr<IPacketReceiver> receiver) {
    auto ret = common::UdpSocket();
    if (ret.errno_ != 0) {
        common::LOG_ERROR("create udp socket failed. err:%d", ret.errno_);
        abort();
        return 0;
    }
    
    auto sock = ret.return_value_;

    // set noblocking
    auto opt_ret = common::SocketNoblocking(sock);
    if (opt_ret.errno_ != 0) {
        common::LOG_ERROR("udp socket noblocking failed. err:%d", opt_ret.errno_);
        abort();
        return 0;
    }

    common::Address addr(ip, port);

    if (ecn_enabled_) {
        // enable receiving TOS/TCLASS for ECN via io_handle abstraction
        common::EnableUdpEcn(sock);
    }

    opt_ret = Bind(sock, addr);
    if (opt_ret.errno_ != 0) {
        common::LOG_ERROR("bind address failed. err:%d", opt_ret.errno_);
        abort();
    }
    if (auto event_loop = event_loop_.lock()) {
        event_loop->RegisterFd(sock, common::EventType::ET_READ, shared_from_this());
    }

    receiver_map_[sock] = receiver;
    return sock;
}

void UdpReceiver::OnRead(uint32_t fd) {
    std::shared_ptr<NetPacket> pkt = packet_allotor_->Malloc();

    auto buffer = pkt->GetData();
    auto span = buffer->GetWriteSpan();

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
    receiver_map_.erase(fd);
    if (auto event_loop = event_loop_.lock()) {
        event_loop->RemoveFd(fd);
    }
    common::LOG_INFO("udp receiver closed. fd:%d", fd);
}

}
}
