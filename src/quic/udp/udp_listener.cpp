#include <memory>

#include "common/log/log.h"
#include "common/buffer/buffer.h"
#include "common/network/io_handle.h"
#include "common/alloter/pool_block.h"

#include "quic/udp/udp_listener.h"
#include "quic/common/constants.h"


namespace quicx {

UdpListener::UdpListener():
    _listen_sock(0) {

}

UdpListener::~UdpListener() {
    if (_listen_sock > 0) {
        Close(_listen_sock);
    }
}

bool UdpListener::Listen(const std::string& ip, uint16_t port) {
    auto ret = UdpSocket();
    if (ret.errno_ != 0) {
        LOG_ERROR("create udp socket failed. err:%d", ret.errno_);
        return false;
    }
    
    uint64_t _listen_sock = ret._return_value;

    _listen_address.SetIp(ip);
    _listen_address.SetPort(port);

    auto bind_ret = Bind(_listen_sock, _listen_address);
    if (bind_ret.errno_ != 0) {
        LOG_ERROR("bind address failed. err:%d", ret.errno_);
        return false;
    }
    
    return true;
}

bool UdpListener::DoRecv(std::shared_ptr<UdpPacketIn> udp_packet) {
    auto buffer = udp_packet->GetData();
    auto span = buffer->GetWriteSpan();
    Address peer_addr(AT_IPV4);
        
    auto recv_ret = RecvFrom(_listen_sock, (char*)span.GetStart(), __max_v4_packet_size, 0, peer_addr);
    if (recv_ret.errno_ != 0) {
        LOG_ERROR("recv from failed. err:%d", recv_ret.errno_);
        return false;
    }
    buffer->MoveReadPt(recv_ret._return_value);
    udp_packet->SetPeerAddress(std::move(peer_addr));

    return true;
}

}
