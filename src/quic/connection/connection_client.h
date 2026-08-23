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
    ClientConnection(std::shared_ptr<TLSCtx> ctx, std::shared_ptr<common::IEventLoop> loop,
        const ConnectionCallbacks& callbacks = {}, bool ecn_enabled = false);
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
    virtual bool OnHandshakePacket(const std::shared_ptr<IPacket>& packet) override;
    virtual bool OnRetryPacket(const std::shared_ptr<IPacket>& packet) override;
    virtual void WriteCryptoData(std::shared_ptr<IBufferRead> buffer, int32_t err, uint16_t encryption_level) override;

    // HANDSHAKE_DONE frame handler (set as callback to frame processor)
    bool HandleHandshakeDoneFrame(std::shared_ptr<IFrame> frame);

    // RFC 9000 §4.1.2: confirm the handshake when a 1-RTT packet is received,
    // not only on HANDSHAKE_DONE.  Implements the base-class hook
    // OnApplicationDataPacketProcessed().
    void OnApplicationDataPacketProcessed() override;

    // RFC 9000 §7.3: in addition to the shared initial_source_connection_id check,
    // the client must verify original_destination_connection_id and, after a Retry,
    // retry_source_connection_id. These are the checks that bind the handshake to the
    // CIDs the client actually chose/observed, defeating forged Retry downgrades.
    virtual bool ValidatePeerConnectionIds(const TransportParam& remote_tp) override;

private:
    // Common TLS setup for both Dial() overloads (ALPN, SNI, transport params)
    bool DialSetupTLS(std::shared_ptr<TLSClientConnection> tls_conn, const common::Address& addr,
        const std::string& alpn, const QuicTransportParams& tp_config, const std::string& server_name);
    // Common finalization for both Dial() overloads (CID generation, secrets, handshake, qlog)
    bool DialFinalize(std::shared_ptr<TLSClientConnection> tls_conn, const common::Address& addr);

    // Original Destination Connection ID (for Retry handling per RFC 9000)
    ConnectionID original_dcid_;
    bool retry_received_{false};
    // SCID carried by the Retry packet we accepted; compared against the server's
    // retry_source_connection_id transport parameter.
    std::string retry_scid_;

    // RFC 9000 §19.20: HANDSHAKE_DONE is ack-eliciting and the server WILL
    // retransmit it whenever our ACK is lost or delayed. Every side effect of
    // handling it (discarding the Initial/Handshake number spaces, exporting
    // the resumption session, invoking handshake_done_cb_) must therefore run
    // exactly once. Without this guard the duplicate frames re-fire
    // handshake_done_cb_, which makes ClientWorker re-announce
    // kConnectionCreate to the application layer for an already-established
    // connection.
    bool handshake_done_processed_{false};

    // TLS settings for Retry (need to restore after Reset)
    std::string saved_alpn_;
    std::string saved_server_name_;
};

}  // namespace quic
}  // namespace quicx

#endif