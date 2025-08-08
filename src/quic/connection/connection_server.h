#ifndef QUIC_CONNECTION_SERVER_CONNECTION
#define QUIC_CONNECTION_SERVER_CONNECTION

#include <string>
#include <memory>
#include <cstdint>
#include "common/timer/if_timer.h"
#include "quic/connection/connection_base.h"
#include "quic/crypto/tls/tls_conneciton_server.h"

namespace quicx {
namespace quic {

class ServerConnection:
    public BaseConnection,
    public TlsServerHandlerInterface {
public:
    ServerConnection(std::shared_ptr<TLSCtx> ctx,
        const std::string& alpn,
        std::shared_ptr<common::ITimer> timer,
        std::function<void(std::shared_ptr<IConnection>)> active_connection_cb,
        std::function<void(std::shared_ptr<IConnection>)> handshake_done_cb,
        std::function<void(ConnectionID&, std::shared_ptr<IConnection>)> add_conn_id_cb,
        std::function<void(ConnectionID&)> retire_conn_id_cb,
        std::function<void(std::shared_ptr<IConnection>, uint64_t error, const std::string& reason)> connection_close_cb);
    virtual ~ServerConnection();

    virtual void AddRemoteConnectionId(ConnectionID& id);

protected:
    virtual bool OnHandshakeDoneFrame(std::shared_ptr<IFrame> frame);
    virtual bool OnRetryPacket(std::shared_ptr<IPacket> packet);
    virtual void WriteCryptoData(std::shared_ptr<common::IBufferRead> buffer, int32_t err);

private:
    void SSLAlpnSelect(const unsigned char **out, unsigned char *outlen,
        const unsigned char *in, unsigned int inlen, void *arg);
private:
    std::string server_alpn_;
};

}
}

#endif