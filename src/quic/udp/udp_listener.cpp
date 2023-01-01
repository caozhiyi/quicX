#include <memory>

#include "common/log/log.h"
#include "common/network/io_handle.h"
#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_read.h"

#include "quic/udp/udp_listener.h"
#include "quic/common/constants.h"


namespace quicx {

UdpListener::UdpListener(std::function<void(std::shared_ptr<IBufferRead>)> cb):
    _listen_sock(0),
    _stop(false),
    _recv_callback(cb) {

}

UdpListener::~UdpListener() {

}

bool UdpListener::Listen(const std::string& ip, uint16_t port) {
    auto ret = UdpSocket();
    if (ret.errno_ != 0) {
        LOG_ERROR("create udp socket failed. err:%d", ret.errno_);
        return false;
    }
    
    uint64_t sock = ret._return_value;

    Address addr(AT_IPV4, ip, port);
    auto bind_ret = Bind(sock, addr);
    if (bind_ret.errno_ != 0) {
        LOG_ERROR("bind address failed. err:%d", ret.errno_);
        return false;
    }
    
    auto alloter = MakeBlockMemoryPoolPtr(__max_v4_packet_size, 10);
    Address peer_addr(AT_IPV4);

    while (!_stop) {
        uint8_t* data = (uint8_t*)alloter->PoolLargeMalloc();
        
        auto recv_ret = RecvFrom(sock, (char*)data, __max_v4_packet_size, 0, peer_addr);
        if (recv_ret.errno_ != 0) {
            LOG_ERROR("recv from failed. err:%d", recv_ret.errno_);
            continue;
        }
        LOG_DEBUG("recv udp msg. msg:%s", data);
        LOG_DEBUG("recv udp msg. size:%d", recv_ret._return_value);
        auto recv_buffer = std::make_shared<BufferRead>(data, data + recv_ret._return_value, alloter);
        _recv_callback(recv_buffer);
    }
    return true;
}

bool UdpListener::Stop() {
    if (_listen_sock > 0) {
        auto ret = Close(_listen_sock);
        if (ret.errno_ != 0) {
            LOG_ERROR("clost udp socket failed. err:%d", ret.errno_);
            return false;
        }
    }
    _stop = true;
    return true;
}

}
