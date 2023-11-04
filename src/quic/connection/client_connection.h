#ifndef QUIC_CONNECTION_CLIENT_CONNECTION
#define QUIC_CONNECTION_CLIENT_CONNECTION

#include <memory>
#include <cstdint>
#include <functional>
#include "quic/connection/type.h"
#include "common/network/address.h"
#include "quic/connection/base_connection.h"
#include "quic/crypto/tls/tls_client_conneciton.h"

namespace quicx {

class ClientConnection;
typedef std::function<void(ClientConnection&)> HandshakeDoneCB;

// TODO
// 1. 创建一个连接
// 2. 如果支持, 启用早期数据
// 3. 当早期数据被服务端接收或拒绝时, 收到通知
class ClientConnection:
    public BaseConnection {
public:
    ClientConnection(std::shared_ptr<TLSCtx> ctx, std::shared_ptr<ITimer> timer);
    ~ClientConnection();

    // set application protocol
    void AddAlpn(AlpnType at);

    // set transport param
    void AddTransportParam(TransportParamConfig& tp_config);

    bool Dial(const Address& addr);

    void Close();

    void SetHandshakeDoneCB(HandshakeDoneCB& cb);
protected:
    virtual bool On0rttPacket(std::shared_ptr<IPacket> packet);
    virtual bool OnRetryPacket(std::shared_ptr<IPacket> packet);

    virtual void WriteCryptoData(std::shared_ptr<IBufferChains> buffer, int32_t err);

private:
    AlpnType _alpn_type; // application protocol
    Address _local_addr;
    Address _peer_addr;

    HandshakeDoneCB _handshake_done_cb;
    std::shared_ptr<TLSClientConnection> _tls_connection;
};

}

#endif