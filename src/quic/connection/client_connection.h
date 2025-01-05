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

class ClientConnection:
    public BaseConnection {
public:
    ClientConnection(std::shared_ptr<TLSCtx> ctx,
        std::shared_ptr<common::ITimer> timer,
        std::function<void(std::shared_ptr<IConnection>)> active_connection_cb,
        std::function<void(std::shared_ptr<IConnection>)> handshake_done_cb,
        std::function<void(uint64_t cid_hash, std::shared_ptr<IConnection>)> add_conn_id_cb,
        std::function<void(uint64_t cid_hash)> retire_conn_id_cb);
    ~ClientConnection();

    // set application protocol
    void AddAlpn(AlpnType at);

    bool Dial(const common::Address& addr);

protected:
    virtual bool OnRetryPacket(std::shared_ptr<IPacket> packet);

private:
    AlpnType alpn_type_; // application protocol
};

}
}

#endif