#ifndef QUIC_CONNECTION_CLIENT_CONNECTION
#define QUIC_CONNECTION_CLIENT_CONNECTION

#include <memory>
#include <cstdint>
#include <functional>
#include "quic/connection/type.h"
#include "common/network/address.h"
#include "quic/stream/crypto_stream.h"
#include "common/alloter/pool_block.h"
#include "quic/frame/frame_interface.h"
#include "quic/connection/connection_interface.h"
#include "quic/crypto/tls/tls_client_conneciton.h"

namespace quicx {

class ClientConnection;
typedef std::function<void(ClientConnection&)> HandshakeDoneCB;

// TODO
// 1. 创建一个连接
// 2. 如果支持, 启用早期数据
// 3. 当早期数据被服务端接收或拒绝时, 收到通知
class ClientConnection:
    public IConnection {
public:
    ClientConnection(std::shared_ptr<TLSCtx> ctx);
    ~ClientConnection();

    // set application protocol
    void AddAlpn(AlpnType at);

    // set transport param
    void AddTransportParam(TransportParamConfig& tp_config);

    bool Dial(const Address& addr);

    void Close();

    virtual bool TrySendData(IPacketVisitor* pkt_visitor);

    void SetHandshakeDoneCB(HandshakeDoneCB& cb);
protected:
    virtual bool HandleInitial(std::shared_ptr<InitPacket> packet);
    virtual bool Handle0rtt(std::shared_ptr<Rtt0Packet> packet);
    virtual bool HandleHandshake(std::shared_ptr<HandShakePacket> packet);
    virtual bool HandleRetry(std::shared_ptr<RetryPacket> packet);
    virtual bool Handle1rtt(std::shared_ptr<Rtt1Packet> packet);

    virtual void WriteMessage(EncryptionLevel level, const uint8_t *data, size_t len);

private:
    AlpnType _alpn_type; // application protocol
    uint64_t _sock;
    Address _local_addr;
    Address _peer_addr;
    StreamIDGenerator _id_generator;
    std::shared_ptr<CryptoStream> _crypto_stream;

    HandshakeDoneCB _handshake_done_cb;
    std::shared_ptr<BlockMemoryPool> _alloter;
    std::shared_ptr<TLSClientConnection> _tls_connection;
};

}

#endif