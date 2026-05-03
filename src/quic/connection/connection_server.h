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
    ServerConnection(std::shared_ptr<TLSCtx> ctx, std::shared_ptr<common::IEventLoop> loop,
        const std::string& alpn, const ConnectionCallbacks& callbacks = {});
    virtual ~ServerConnection();

    virtual void AddRemoteConnectionId(ConnectionID& id);

protected:
    virtual bool OnRetryPacket(std::shared_ptr<IPacket> packet) override;
    virtual void WriteCryptoData(std::shared_ptr<IBufferRead> buffer, int32_t err, uint16_t encryption_level) override;

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