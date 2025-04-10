#ifndef QUIC_CONNECTION_CLIENT_CONNECTION
#define QUIC_CONNECTION_CLIENT_CONNECTION

#include <memory>
#include <cstdint>
#include <functional>
#include "quic/connection/type.h"
#include "common/network/address.h"
#include "quic/connection/connection_base.h"
#include "quic/crypto/tls/tls_conneciton_client.h"

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
        std::function<void(uint64_t cid_hash)> retire_conn_id_cb,
        std::function<void(std::shared_ptr<IConnection>, uint64_t error, const std::string& reason)> connection_close_cb);
    ~ClientConnection();

    bool Dial(const common::Address& addr, const std::string& alpn, const QuicTransportParams& tp_config);

protected:
    virtual bool OnHandshakePacket(std::shared_ptr<IPacket> packet);
    virtual bool OnHandshakeDoneFrame(std::shared_ptr<IFrame> frame);
    virtual bool OnRetryPacket(std::shared_ptr<IPacket> packet);
    virtual void WriteCryptoData(std::shared_ptr<common::IBufferRead> buffer, int32_t err);
};

}
}

#endif