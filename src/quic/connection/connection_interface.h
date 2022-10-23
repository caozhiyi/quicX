#ifndef QUIC_CONNECTION_CONNECTION_INTERFACE
#define QUIC_CONNECTION_CONNECTION_INTERFACE

#include <cstdint>
#include "quic/crypto/ssl_conneciton.h"
#include "quic/connection/transport_param.h"

namespace quicx {

class Connection {
public:
    Connection() {}
    virtual ~Connection() {}
    // TODO 
    // 1. 为流配置允许的最小初始数量
    // 2. 设置流级别及连接级别的流量限制, 限制接收缓存的的大小
    // 3. 识别握手成功或进行中
    // 4. 保持连接不被关闭, 发送PING等帧
    // 5. 立即关闭连接
    virtual void Close() = 0;
protected:
    //SSLConnection  _ssl_connection;
    TransportParam _transport_param;
};

}

#endif