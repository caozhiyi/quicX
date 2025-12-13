#ifndef QUIC_CONNECTION_SERVER_CONNECTION
#define QUIC_CONNECTION_SERVER_CONNECTION

#include <cstdint>
#include <memory>
#include <string>

#include "quic/connection/connection_base.h"
#include "quic/crypto/tls/tls_connection_server.h"

namespace quicx {
namespace quic {

class ServerConnection: public BaseConnection, public TlsServerHandlerInterface {
public:
    ServerConnection(std::shared_ptr<TLSCtx> ctx, std::shared_ptr<common::IEventLoop> loop, const std::string& alpn,
        std::function<void(std::shared_ptr<IConnection>)> active_connection_cb,
        std::function<void(std::shared_ptr<IConnection>)> handshake_done_cb,
        std::function<void(ConnectionID&, std::shared_ptr<IConnection>)> add_conn_id_cb,
        std::function<void(ConnectionID&)> retire_conn_id_cb,
        std::function<void(std::shared_ptr<IConnection>, uint64_t error, const std::string& reason)>
            connection_close_cb);
    virtual ~ServerConnection();

    virtual void AddRemoteConnectionId(ConnectionID& id);

protected:
    virtual bool OnRetryPacket(std::shared_ptr<IPacket> packet) override;
    virtual void WriteCryptoData(std::shared_ptr<IBufferRead> buffer, int32_t err) override;

    // HANDSHAKE_DONE frame handler (set as callback to frame processor)
    bool HandleHandshakeDoneFrame(std::shared_ptr<IFrame> frame);

private:
    virtual void SSLAlpnSelect(const unsigned char** out, unsigned char* outlen, const unsigned char* in,
        unsigned int inlen, void* arg) override;

    std::string server_alpn_;
};

}  // namespace quic
}  // namespace quicx

#endif