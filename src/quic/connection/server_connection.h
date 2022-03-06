#ifndef QUIC_CONNECTION_SERVER_CONNECTION
#define QUIC_CONNECTION_SERVER_CONNECTION

#include <memory>
#include <cstdint>
#include "quic/connection/connection_interface.h"

namespace quicx {

class Frame;
class Address;
class ServerConnection: public Connection {
public:
    ServerConnection() {}
    virtual ~ServerConnection() {}

    // TODO
    // 1. 监听传入的连接
    // 2. 如果支持早期数据，在发送给客户端的TLS恢复ticket中嵌入应用层控制数据
    // 3. 如果支持早期数据，从接收自客户端的恢复ticket中恢复应用层控制数据，并根据该信息接受或拒绝早期数据。
};

}

#endif