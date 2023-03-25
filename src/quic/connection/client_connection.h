#ifndef QUIC_CONNECTION_CLIENT_CONNECTION
#define QUIC_CONNECTION_CLIENT_CONNECTION

#include <memory>
#include <cstdint>
#include "common/network/address.h"
#include "quic/connection/connection_interface.h"

namespace quicx {

class IFrame;
class ClientConnection:
    public IConnection {
public:
    ClientConnection();
    ClientConnection(TransportParamConfig& tpc);
    virtual ~ClientConnection();

    virtual bool Dial(const Address& addr);

    virtual void Close();
    // TODO
    // 1. 创建一个连接
    // 2. 如果支持, 启用早期数据
    // 3. 当早期数据被服务端接收或拒绝时, 收到通知
private:
    uint64_t _sock;
    Address _local_addr;
    Address _peer_addr;
};

}

#endif