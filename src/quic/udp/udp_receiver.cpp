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

#ifdef __linux__
#include "quic/udp/action/epoll/udp_action.h"
#endif
#ifdef _WIN32
#include "quic/udp//action/select/udp_action.h"
#endif
#ifdef __APPLE__
#include "quic/udp/action/kqueue/udp_action.h"
#endif

namespace quicx {
namespace quic {

UdpReceiver::UdpReceiver() {
    action_ = std::make_shared<UdpAction>();
}

UdpReceiver::~UdpReceiver() {

}

void UdpReceiver::AddReceiver(int32_t socket_fd) {
    // set noblocking
    auto opt_ret = common::SocketNoblocking(socket_fd);
    if (opt_ret.errno_ != 0) {
        common::LOG_ERROR("udp socket noblocking failed. err:%d", opt_ret.errno_);
        abort();
    }

    action_->AddSocket(socket_fd);
}

int32_t UdpReceiver::AddReceiver(const std::string& ip, uint16_t port) {
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
    action_->AddSocket(sock);
    return sock;
}

void UdpReceiver::TryRecv(std::shared_ptr<NetPacket>& pkt, uint32_t timeout_ms) {
    // try recv, if there is socket has data
    if (TryRecv(pkt)) {
        return;
    }
    
    // we can't receive any data, so we need to wait
    action_->Wait(timeout_ms, socket_queue_);

    // try again, check if weakup by data receiveing
    TryRecv(pkt);
}

void UdpReceiver::Wakeup() {
    action_->Wakeup();
}

bool UdpReceiver::TryRecv(std::shared_ptr<NetPacket>& pkt) {
    while (!socket_queue_.empty()) {
        int32_t sockfd = socket_queue_.front();
        socket_queue_.pop();

        auto buffer = pkt->GetData();
        auto span = buffer->GetWriteSpan();

        // Use platform abstraction to capture ECN and peer address
        common::Address peer_addr;
        uint8_t ecn = 0;
        auto ret = common::RecvFromWithEcn(sockfd, (char*)span.GetStart(), kMaxV4PacketSize, 0, peer_addr, ecn);
        if (ret.errno_ != 0) {
            if (ret.errno_ == EAGAIN) {
                continue;
            }
            common::LOG_ERROR("recv from failed. err:%d", ret.errno_);
            continue;
        }
        common::LOG_DEBUG("recv from data from peer. addr: %s, size:%d", peer_addr.AsString().c_str(), ret.return_value_);
        buffer->MoveWritePt(ret.return_value_);
        pkt->SetAddress(std::move(peer_addr));
        pkt->SetSocket(sockfd);
        pkt->SetTime(common::UTCTimeMsec());
        pkt->SetEcn(ecn_enabled_ ? ecn : 0);
        return true;
    }
    return false;
}

}
}
