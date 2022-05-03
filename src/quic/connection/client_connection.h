#ifndef QUIC_CONNECTION_CLIENT_CONNECTION
#define QUIC_CONNECTION_CLIENT_CONNECTION

#include <memory>
#include <cstdint>
#include "quic/connection/connection_interface.h"

namespace quicx {

class IFrame;
class Address;
class ClientConnection:
    public Connection {
public:
    ClientConnection() {}
    virtual ~ClientConnection() {}

    virtual bool Open(Address& addr, uint32_t strean_limit) = 0;

    // TODO
    // 1. 创建一个连接
    // 2. 如果支持, 启用早期数据
    // 3. 当早期数据被服务端接收或拒绝时, 收到通知
};

}

#endif