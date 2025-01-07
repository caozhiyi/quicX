#ifndef QUIC_CONNECTION_SERVER_CONNECTION
#define QUIC_CONNECTION_SERVER_CONNECTION

#include <memory>
#include <cstdint>
#include "common/timer/if_timer.h"
#include "quic/connection/base_connection.h"
#include "quic/crypto/tls/tls_server_conneciton.h"

namespace quicx {
namespace quic {

class ServerConnection:
    public BaseConnection,
    public TlsServerHandlerInterface {
public:
    ServerConnection(std::shared_ptr<TLSCtx> ctx,
        std::shared_ptr<common::ITimer> timer,
        std::function<void(std::shared_ptr<IConnection>)> active_connection_cb,
        std::function<void(std::shared_ptr<IConnection>)> handshake_done_cb,
        std::function<void(uint64_t cid_hash, std::shared_ptr<IConnection>)> add_conn_id_cb,
        std::function<void(uint64_t cid_hash)> retire_conn_id_cb,
        std::function<void(std::shared_ptr<IConnection>, uint64_t error, const std::string& reason)> connection_close_cb);
    virtual ~ServerConnection();

    virtual void AddRemoteConnectionId(uint8_t* id, uint16_t len);

protected:
    virtual bool OnHandshakeDoneFrame(std::shared_ptr<IFrame> frame);
    virtual bool OnRetryPacket(std::shared_ptr<IPacket> packet);

private:
    void SSLAlpnSelect(const unsigned char **out, unsigned char *outlen,
        const unsigned char *in, unsigned int inlen, void *arg);
};

}
}

#endif