#include <memory>
#include <cstdlib>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#endif
#include "common/log/log.h"
#include "common/util/time.h"
#include "common/buffer/buffer.h"
#include "quic/udp/udp_receiver.h"
#include "quic/common/constants.h"
#include "common/network/io_handle.h"
#include "common/alloter/pool_block.h"

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

void UdpReceiver::AddReceiver(uint64_t socket_fd) {
    action_->AddSocket(socket_fd);
}

void UdpReceiver::AddReceiver(const std::string& ip, uint16_t port) {
    auto ret = common::UdpSocket();
    if (ret.errno_ != 0) {
        common::LOG_ERROR("create udp socket failed. err:%d", ret.errno_);
        abort();
        return;
    }
    
    auto sock = ret.return_value_;

    // set noblocking
    auto opt_ret = common::SocketNoblocking(sock);
    if (opt_ret.errno_ != 0) {
        common::LOG_ERROR("udp socket noblocking failed. err:%d", opt_ret.errno_);
        abort();
        return;
    }

    common::Address addr(ip, port);

    opt_ret = Bind(sock, addr);
    if (opt_ret.errno_ != 0) {
        common::LOG_ERROR("bind address failed. err:%d", opt_ret.errno_);
        abort();
    }
    action_->AddSocket(sock);
}

void UdpReceiver::TryRecv(std::shared_ptr<NetPacket> pkt, uint32_t timeout_ms) {
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

bool UdpReceiver::TryRecv(std::shared_ptr<NetPacket> pkt) {
    while (!socket_queue_.empty()) {
        uint64_t sock = socket_queue_.front();
        socket_queue_.pop();

        auto buffer = pkt->GetData();
        auto span = buffer->GetWriteSpan();
        common::Address peer_addr;

        auto ret = common::RecvFrom(sock, (char*)span.GetStart(), kMaxV4PacketSize, 0, peer_addr);
        if (ret.errno_ != 0) {
            if (ret.errno_ == EAGAIN) {
                continue;
            }
            common::LOG_ERROR("recv from failed. err:%d", ret.errno_);
            continue;
        }
        buffer->MoveWritePt(ret.return_value_);
        pkt->SetAddress(std::move(peer_addr));
        pkt->SetSocket(sock);
        pkt->SetTime(common::UTCTimeMsec());
        return true;
    }
    return false;
}

}
}
