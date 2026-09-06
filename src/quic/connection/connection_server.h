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
        const ConnectionCallbacks& callbacks = {}, bool ecn_enabled = false);
    ~ServerConnection() = default;

    virtual void AddRemoteConnectionId(ConnectionID& id);

protected:
    virtual bool OnRetryPacket(const std::shared_ptr<IPacket>& packet) override;

    // WriteCryptoData 的握手完成钩子（Base 负责公共的 TLS 数据回灌部分）
    virtual void OnTlsHandshakeComplete() override;

    // HANDSHAKE_DONE frame handler (set as callback to frame processor)
    bool HandleHandshakeDoneFrame(std::shared_ptr<IFrame> frame);

private:
    virtual void SSLAlpnSelect(const unsigned char** out, unsigned char* outlen, const unsigned char* in,
        unsigned int inlen, void* arg) override;

    // Emit a fresh HANDSHAKE_DONE frame (first copy and loss-recovery copies
    // alike), attached to the frame-level delivery tracking so its loss is
    // repaired by the standard loss-detection machinery instead of the removed
    // duplicate-Finished sniffing patch.
    void SendHandshakeDoneFrame();

    std::string server_alpn_;

    // DoHandleShake() reports completion every time it re-processes the
    // client's Finished (a retransmitted CRYPTO frame under loss makes it
    // return true again). Emitting HANDSHAKE_DONE, discarding the Initial /
    // Handshake spaces and notifying the application a second time rebuilds
    // the HTTP/3 server context, which drops the request callback already
    // registered on it -- the request then arrives at a connection that never
    // answers it (interop handshakecorruption vs quinn: one file per run
    // downloads 0 bytes). The whole completion path must run exactly once.
    bool handshake_completion_emitted_ = false;

    // Set by the HANDSHAKE_DONE frame's delivery handler when any copy of the
    // frame has been acknowledged; once true, later kLost notifications of
    // stale copies stop re-queuing fresh ones.
    bool handshake_done_acked_ = false;
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_SERVER_CONNECTION