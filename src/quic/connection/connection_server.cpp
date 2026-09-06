#include "common/log/log.h"
#include "common/qlog/qlog.h"
#include "common/util/time.h"

#include "quic/connection/connection_frame_processor.h"
#include "quic/connection/connection_server.h"
#include "quic/connection/connection_stream_manager.h"
#include "quic/connection/connection_timer_coordinator.h"
#include "quic/connection/error.h"
#include "quic/crypto/tls/type.h"
#include "quic/frame/handshake_done_frame.h"
#include "quic/frame/if_frame.h"
#include "quic/frame/type.h"

namespace quicx {
namespace quic {

ServerConnection::ServerConnection(std::shared_ptr<TLSCtx> ctx, std::shared_ptr<common::IEventLoop> loop,
    const std::string& alpn, const ConnectionCallbacks& callbacks, bool ecn_enabled):
    BaseConnection(StreamIDGenerator::StreamStarter::kServer, ecn_enabled, loop, callbacks),
    server_alpn_(alpn) {
    tls_connection_ = std::make_shared<TLSServerConnection>(ctx, &connection_crypto_, this);
    if (!tls_connection_->Init()) {
        LOG_ERROR("tls connection init failed.");
    }
    auto crypto_stream = std::make_shared<CryptoStream>(
        event_loop_, [this](auto a) { OnStreamDataReady(a); }, [this](auto a) { InnerStreamClose(a); },
        [this](auto a, auto b, auto c) { InnerConnectionClose(a, b, c); });
    crypto_stream->SetCryptoStreamReadCallBack([this](auto a, auto b, auto c) { WriteCryptoData(a, b, c); });

    connection_crypto_.SetCryptoStream(crypto_stream);

    // Set HANDSHAKE_DONE frame handler callback (returns bool)
    frame_processor_->SetHandshakeDoneCallback([this](auto a) { return HandleHandshakeDoneFrame(a); });

    // RFC 9000 §8.1, secure by default: a server connection is born from an
    // Initial whose source address could be spoofed, so start under the 3x
    // budget and make the caller opt out via MarkAddressValidated() once the
    // address is actually proven (a valid Retry token). The inverse default --
    // start unrestricted and rely on the accepting worker to remember to
    // restrict -- is what let the limit sit dead in the first place: a dropped
    // call there is silently insecure, whereas a dropped MarkAddressValidated()
    // merely throttles and shows up immediately in tests.
    EnterUnvalidatedAddressState();
}

// ~ServerConnection is defaulted; qlog trace cleanup runs in ~BaseConnection
// via GetQlogTraceIdForCleanup.

void ServerConnection::AddRemoteConnectionId(ConnectionID& id) {
    cid_coordinator_->GetRemoteConnectionIDManager()->AddID(id);

    // Create qlog trace for this connection (if not already created)
    if (!qlog_trace_ && common::QlogManager::Instance().IsEnabled()) {
        common::ConnectionStartedData data;
        // Server gets address from received packet
        auto peer_addr = GetPeerAddress();
        data.src_ip = peer_addr.GetIp();
        data.src_port = peer_addr.GetPort();
        // dst_* is the local (listening) endpoint of this connection. Pull it
        // from the bound socket via IConnection::GetLocalAddr, which caches
        // the result in local_addr_ on first lookup so the qlog path is not
        // a hot-path syscall hit.
        std::string local_ip;
        uint32_t local_port = 0;
        GetLocalAddr(local_ip, local_port);
        data.dst_ip = local_ip;
        data.dst_port = local_port;
        // Use hash values for connection IDs
        data.src_cid = std::to_string(id.Hash());
        data.dst_cid = std::to_string(cid_coordinator_->GetLocalConnectionIDManager()->GetCurrentID().Hash());
        data.protocol = "QUIC";
        data.ip_version = peer_addr.IsIPv6() ? "ipv6" : "ipv4";

        InstallQlogTrace(std::to_string(cid_coordinator_->GetLocalConnectionIDManager()->GetCurrentID().Hash()),
            common::VantagePoint::kServer, data);
    }
}

bool ServerConnection::HandleHandshakeDoneFrame(std::shared_ptr<IFrame> /*frame*/) {
    // RFC 9000 §19.20: "A server MUST treat receipt of a HANDSHAKE_DONE
    // frame as a connection error of type PROTOCOL_VIOLATION."
    LOG_ERROR("Server received HANDSHAKE_DONE frame from client - PROTOCOL_VIOLATION");
    InnerConnectionClose(QuicErrorCode::kProtocolViolation, static_cast<uint16_t>(FrameType::kHandshakeDone),
        "server received HANDSHAKE_DONE frame");
    return false;
}

bool ServerConnection::OnRetryPacket(const std::shared_ptr<IPacket>& packet) {
    // Server-initiated Retry (RFC 9000 §17.2.5) is intentionally not
    // implemented: it is an anti-DoS / address-validation feature whose value
    // shows up only at scale, and a complete implementation would pull in a
    // stateless Retry-token signing/verification path plus token replay
    // tracking. Tracked as a learning-only limitation in
    // learning_project_roadmap.md §2.
    //
    // For the same reason, a server here MUST never *receive* a Retry — only
    // the client does — so this codepath should be unreachable in practice.
    // We swallow the packet rather than tear down the connection, mirroring
    // RFC 9000 §17.2.5.2's "If a server receives a client packet that is not
    // expected, it can simply discard it." stance.
    (void)packet;
    return true;
}

void ServerConnection::OnTlsHandshakeComplete() {
    // DoHandleShake() reports completion every time it re-processes the
    // client's Finished (a retransmitted CRYPTO frame under loss makes it
    // return true again). The whole completion path below must run exactly
    // once — see handshake_completion_emitted_ in the header.
    if (handshake_completion_emitted_) {
        return;
    }
    handshake_completion_emitted_ = true;

    LOG_DEBUG("handshake done.");
    // RFC 9000 §19.20 / §4.1.1: "The server MUST send a HANDSHAKE_DONE
    // frame as soon as the handshake is complete." BaseConnection calls this
    // hook exactly when DoHandleShake() first reports the BoringSSL state
    // where the server has just consumed the client's Finished — i.e. the
    // handshake is complete from the server's point of view — so emitting
    // the frame here, before any other post-handshake bookkeeping,
    // satisfies "as soon as".
    SendHandshakeDoneFrame();

    // RFC 9000 §4.1.2: from here until the peer's first 1-RTT ACK there is
    // no evidence it ever saw this frame. The frame's delivery handler now
    // repairs loss through the standard loss-detection signals (see
    // SendHandshakeDoneFrame); the wider idle timeout keeps the window
    // open while the peer's own PTO retries land.
    timer_coordinator_->EnterHandshakeConfirmGrace();

    // Stop PTO probing, drop Initial/Handshake number spaces (RFC 9000
    // §4.10), qlog key_discarded events — shared with the client's
    // confirmation path.
    FinalizeHandshakePacketNumberSpaces();

    state_machine_.OnHandshakeDone();
    // notify handshake done
    if (handshake_done_cb_) {
        handshake_done_cb_(shared_from_this());
    }
}

void ServerConnection::SendHandshakeDoneFrame() {
    // RFC 9000 §19.20: HANDSHAKE_DONE is ack-eliciting and the server is
    // expected to keep sending it until the client confirms. Historically that
    // reliability was faked by sniffing duplicate Handshake CRYPTO (the peer's
    // Finished replay) as a resend trigger; that patch is now replaced by
    // frame-level delivery tracking (aioquic's _on_handshake_done_delivery
    // model): the frame carries a handler that SendControl fires when the
    // packet transporting it is acknowledged or declared lost.
    //
    // On kLost the handler re-queues a FRESH frame (which gets its own
    // handler, so the retry chain never breaks) through OnFrameReady, i.e.
    // the regular send loop — no bespoke timers, no heuristic triggers. The
    // loss signals that drive it (packet/time thresholds, PTO) are the
    // transport's own, so recovery cadence matches the path conditions.
    //
    // Duplicate copies are legal and expected (PTO re-queues, coalesced
    // resends): the client treats HANDSHAKE_DONE idempotently (see
    // ClientConnection::handshake_done_processed_), and the original sniffing
    // patch's 10 ms rate-limit becomes unnecessary — acked_ suppresses
    // re-queues only once a copy has actually gotten through.
    std::shared_ptr<HandshakeDoneFrame> frame = std::make_shared<HandshakeDoneFrame>();
    std::weak_ptr<ServerConnection> weak = std::static_pointer_cast<ServerConnection>(this->shared_from_this());
    frame->SetDeliveryHandler([weak](FrameDeliveryState state) {
        std::shared_ptr<ServerConnection> self = weak.lock();
        if (!self) {
            return;
        }
        if (state == FrameDeliveryState::kAcked) {
            if (!self->handshake_done_acked_) {
                self->handshake_done_acked_ = true;
                LOG_DEBUG("Server: HANDSHAKE_DONE acknowledged");
            }
            return;
        }
        if (self->handshake_done_acked_) {
            // A stale copy of an already-acknowledged frame was declared lost.
            return;
        }
        LOG_INFO("Server: HANDSHAKE_DONE lost - re-queueing a fresh copy");
        self->SendHandshakeDoneFrame();
    });
    OnFrameReady(frame);
}

void ServerConnection::SSLAlpnSelect(
    const unsigned char** out, unsigned char* outlen, const unsigned char* in, unsigned int inlen, void* /*arg*/) {
    // parse client alpn list
    // ALPN format: [length1][protocol1][length2][protocol2]...
    std::vector<std::string> client_protos;
    for (unsigned int i = 0; i < inlen;) {
        if (i >= inlen) {
            break;
        }
        unsigned char len = in[i++];
        if (i + len > inlen) {
            LOG_ERROR("invalid ALPN format: length %d exceeds remaining data", len);
            break;
        }
        std::string proto((const char*)&in[i], len);
        client_protos.push_back(proto);
        LOG_DEBUG("client alpn:%s", proto.c_str());
        i += len;
    }
    LOG_DEBUG("server alpn:%s", server_alpn_.c_str());

    // find a matching alpn
    for (auto const& client_proto : client_protos) {
        if (client_proto == server_alpn_) {
            *out = (unsigned char*)server_alpn_.c_str();
            *outlen = server_alpn_.length();
            LOG_DEBUG("ALPN selected: %s", server_alpn_.c_str());
            return;
        }
    }

    LOG_ERROR("no alpn found. server alpn:%s (len:%d)", server_alpn_.c_str(), server_alpn_.length());
    for (size_t i = 0; i < server_alpn_.length(); ++i) {
        LOG_ERROR("server alpn[%d]: 0x%02x", i, (unsigned char)server_alpn_[i]);
    }

    for (auto const& client_proto : client_protos) {
        LOG_ERROR("client alpn:%s (len:%d)", client_proto.c_str(), client_proto.length());
        for (size_t i = 0; i < client_proto.length(); ++i) {
            LOG_ERROR("client alpn[%d]: 0x%02x", i, (unsigned char)client_proto[i]);
        }
    }

    // No matching ALPN found - signal failure to BoringSSL
    *out = nullptr;
    *outlen = 0;
}

}  // namespace quic
}  // namespace quicx