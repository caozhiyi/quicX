#ifndef QUIC_CONNECTION_CLIENT_CONNECTION
#define QUIC_CONNECTION_CLIENT_CONNECTION

#include <cstdint>
#include <functional>
#include <memory>

#include "common/network/address.h"
#include "quic/connection/connection_base.h"
#include "quic/crypto/tls/tls_connection_client.h"

namespace quicx {
namespace quic {

class ClientConnection: public BaseConnection {
public:
    ClientConnection(std::shared_ptr<TLSCtx> ctx, std::shared_ptr<ISender> sender,
        std::shared_ptr<common::IEventLoop> loop,
        std::function<void(std::shared_ptr<IConnection>)> active_connection_cb,
        std::function<void(std::shared_ptr<IConnection>)> handshake_done_cb,
        std::function<void(ConnectionID&, std::shared_ptr<IConnection>)> add_conn_id_cb,
        std::function<void(ConnectionID&)> retire_conn_id_cb,
        std::function<void(std::shared_ptr<IConnection>, uint64_t error, const std::string& reason)>
            connection_close_cb);
    ~ClientConnection();

    bool Dial(const common::Address& addr, const std::string& alpn, const QuicTransportParams& tp_config,
        const std::string& server_name = "");
    bool Dial(const common::Address& addr, const std::string& alpn, const std::string& resumption_session_der,
        const QuicTransportParams& tp_config, const std::string& server_name = "");
    std::shared_ptr<TLSClientConnection> GetTLSConnection() {
        return std::dynamic_pointer_cast<TLSClientConnection>(tls_connection_);
    }

    bool ExportResumptionSession(std::string& out_session_der);

protected:
    virtual bool OnHandshakePacket(std::shared_ptr<IPacket> packet) override;
    virtual bool OnRetryPacket(std::shared_ptr<IPacket> packet) override;
    virtual void WriteCryptoData(std::shared_ptr<IBufferRead> buffer, int32_t err) override;

    // HANDSHAKE_DONE frame handler (set as callback to frame processor)
    bool HandleHandshakeDoneFrame(std::shared_ptr<IFrame> frame);

private:
    // Original Destination Connection ID (for Retry handling per RFC 9000)
    ConnectionID original_dcid_;
    bool retry_received_{false};

    // TLS settings for Retry (need to restore after Reset)
    std::string saved_alpn_;
    std::string saved_server_name_;
};

}  // namespace quic
}  // namespace quicx

#endif