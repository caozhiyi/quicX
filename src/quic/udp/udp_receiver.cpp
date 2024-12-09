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


namespace quicx {
namespace quic {

UdpReceiver::UdpReceiver(uint64_t sock):
    _sock(sock) {

}

UdpReceiver::UdpReceiver(const std::string& ip, uint16_t port) {
    auto ret = common::UdpSocket();
    if (ret.errno_ != 0) {
        common::LOG_ERROR("create udp socket failed. err:%d", ret.errno_);
        abort();
        return;
    }
    
    _sock = ret._return_value;

    // reuse port
    int opt = 1;
    auto opt_ret = common::SetSockOpt(_sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    if (opt_ret.errno_ != 0) {
        common::LOG_ERROR("udp socket reuseport failed. err:%d", opt_ret.errno_);
        abort();
        return;
    }

    common::Address addr(common::Address::CheckAddressType(ip), ip, port);

    opt_ret = Bind(_sock, addr);
    if (opt_ret.errno_ != 0) {
        common::LOG_ERROR("bind address failed. err:%d", opt_ret.errno_);
        abort();
    }
}

UdpReceiver::~UdpReceiver() {
    if (_sock > 0) {
        common::Close(_sock);
    }
}

IReceiver::RecvResult UdpReceiver::TryRecv(std::shared_ptr<INetPacket> pkt) {
    auto buffer = pkt->GetData();
    auto span = buffer->GetWriteSpan();
    common::Address peer_addr;

    auto ret = common::RecvFrom(_sock, (char*)span.GetStart(), __max_v4_packet_size, MSG_DONTWAIT, peer_addr);
    if (ret.errno_ != 0) {
        if (ret.errno_ == EAGAIN) {
            return IReceiver::RR_NO_DATA;
        }
        
        common::LOG_ERROR("recv from failed. err:%d", ret.errno_);
        return UdpReceiver::RR_FAILED;
    }
    buffer->MoveReadPt(ret._return_value);
    pkt->SetAddress(std::move(peer_addr));
    pkt->SetSocket(_sock);
    pkt->SetTime(common::UTCTimeMsec());

    return UdpReceiver::RR_SUCCESS;
}

}
}
