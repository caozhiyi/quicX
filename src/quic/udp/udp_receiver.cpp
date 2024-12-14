#include <memory>
#include <cstdlib>
#include <sys/socket.h>
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

namespace quicx {
namespace quic {

UdpReceiver::UdpReceiver(uint64_t sock):
    sock_(sock) {
    action_ = std::make_shared<UdpAction>();
    action_->AddSocket(sock_);
}

UdpReceiver::UdpReceiver(const std::string& ip, uint16_t port) {
    auto ret = common::UdpSocket();
    if (ret.errno_ != 0) {
        common::LOG_ERROR("create udp socket failed. err:%d", ret.errno_);
        abort();
        return;
    }
    
    sock_ = ret._return_value;

    // reuse port
    int opt = 1;
    auto opt_ret = common::SetSockOpt(sock_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    if (opt_ret.errno_ != 0) {
        common::LOG_ERROR("udp socket reuseport failed. err:%d", opt_ret.errno_);
        abort();
        return;
    }

    common::Address addr(common::Address::CheckAddressType(ip), ip, port);

    opt_ret = Bind(sock_, addr);
    if (opt_ret.errno_ != 0) {
        common::LOG_ERROR("bind address failed. err:%d", opt_ret.errno_);
        abort();
    }

    action_ = std::make_shared<UdpAction>();
    action_->AddSocket(sock_);
}

UdpReceiver::~UdpReceiver() {
    if (sock_ > 0) {
        common::Close(sock_);
    }
}

void UdpReceiver::TryRecv(std::shared_ptr<INetPacket> pkt, uint32_t timeout_ms) {
    // try recv, if there is socket has data
    if (TryRecv(pkt)) {
        return;
    }
    
    // we can't receive any data, so we need to wait
    action_->Wait(timeout_ms, socket_queue_);

    // try again, check if weakup by data receiveing
    TryRecv(pkt);
}

void UdpReceiver::Weakup() {
    action_->Weakup();
}

bool UdpReceiver::TryRecv(std::shared_ptr<INetPacket> pkt) {
    while (!socket_queue_.empty()) {
        uint64_t sock = socket_queue_.front();
        socket_queue_.pop();

        auto buffer = pkt->GetData();
        auto span = buffer->GetWriteSpan();
        common::Address peer_addr;

        auto ret = common::RecvFrom(sock, (char*)span.GetStart(), __max_v4_packet_size, MSG_DONTWAIT, peer_addr);
        if (ret.errno_ != 0) {
            if (ret.errno_ == EAGAIN) {
                continue;
            }
            common::LOG_ERROR("recv from failed. err:%d", ret.errno_);
            continue;
        }
        buffer->MoveReadPt(ret._return_value);
        pkt->SetAddress(std::move(peer_addr));
        pkt->SetSocket(sock);
        pkt->SetTime(common::UTCTimeMsec());
        return true;
    }
    return false;
}

}
}
