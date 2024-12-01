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
namespace quic {

class ClientConnection;
typedef std::function<void(ClientConnection&)> HandshakeDoneCB;

class ClientConnection:
    public BaseConnection {
public:
    ClientConnection(std::shared_ptr<TLSCtx> ctx,
        std::shared_ptr<common::ITimer> timer,
        std::function<void(uint64_t/*cid hash*/)> add_conn_id_cb,
        std::function<void(uint64_t/*cid hash*/)> retire_conn_id_cb);
    ~ClientConnection();

    // set application protocol
    void AddAlpn(AlpnType at);

    // set transport param
    void AddTransportParam(TransportParamConfig& tp_config);

    bool Dial(const common::Address& addr);

    void SetHandshakeDoneCB(HandshakeDoneCB& cb);
protected:
    virtual bool On0rttPacket(std::shared_ptr<IPacket> packet);
    virtual bool OnRetryPacket(std::shared_ptr<IPacket> packet);

    virtual void WriteCryptoData(std::shared_ptr<common::IBufferChains> buffer, int32_t err);

private:
    AlpnType _alpn_type; // application protocol
    common::Address _local_addr;
    common::Address _peer_addr;

    HandshakeDoneCB _handshake_done_cb;
    std::shared_ptr<TLSClientConnection> _tls_connection;
};

}
}

#endif