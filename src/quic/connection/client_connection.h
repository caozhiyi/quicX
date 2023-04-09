#ifndef QUIC_CONNECTION_CLIENT_CONNECTION
#define QUIC_CONNECTION_CLIENT_CONNECTION

#include <memory>
#include <cstdint>
#include "common/network/address.h"
#include "common/alloter/pool_block.h"
#include "quic/common/send_data_visitor.h"
#include "quic/stream/stream_id_generator.h"
#include "quic/connection/connection_interface.h"
#include "quic/crypto/tls/tls_client_conneciton.h"

namespace quicx {

class IFrame;
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

    bool TrySendData(SendDataVisitor& visitior);
protected:
    virtual bool HandleInitial(std::shared_ptr<InitPacket> packet);
    virtual bool Handle0rtt(std::shared_ptr<Rtt0Packet> packet);
    virtual bool HandleHandshake(std::shared_ptr<HandShakePacket> packet);
    virtual bool HandleRetry(std::shared_ptr<RetryPacket> packet);
    virtual bool Handle1rtt(std::shared_ptr<Rtt1Packet> packet);

private:
    AlpnType _alpn_type; // application protocol
    uint64_t _sock;
    Address _local_addr;
    Address _peer_addr;
    StreamIDGenerator _id_generator;
    std::shared_ptr<IStream> _crypto_stream;
    std::shared_ptr<BlockMemoryPool> _alloter;
    std::shared_ptr<TLSClientConnection> _tls_connection;
};

}

#endif