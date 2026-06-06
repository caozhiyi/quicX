#include "common/log/log.h"
#include "common/qlog/qlog.h"

#include "quic/connection/connection_server.h"
#include "quic/connection/connection_frame_processor.h"
#include "quic/connection/connection_stream_manager.h"
#include "quic/connection/error.h"
#include "quic/frame/handshake_done_frame.h"
#include "quic/frame/if_frame.h"
#include "quic/frame/type.h"

namespace quicx {
namespace quic {

ServerConnection::ServerConnection(std::shared_ptr<TLSCtx> ctx, std::shared_ptr<common::IEventLoop> loop,
    const std::string& alpn, const ConnectionCallbacks& callbacks):
    BaseConnection(StreamIDGenerator::StreamStarter::kServer, false, loop, callbacks),
    server_alpn_(alpn) {
    tls_connection_ = std::make_shared<TLSServerConnection>(ctx, &connection_crypto_, this);
    if (!tls_connection_->Init()) {
        LOG_ERROR("tls connection init failed.");
    }
    auto crypto_stream = std::make_shared<CryptoStream>(event_loop_,
        [this](auto a) { ActiveSendStream(a); },
        [this](auto a) { InnerStreamClose(a); },
        [this](auto a, auto b, auto c) { InnerConnectionClose(a, b, c); });
    crypto_stream->SetCryptoStreamReadCallBack(
        [this](auto a, auto b, auto c) { WriteCryptoData(a, b, c); });

    connection_crypto_.SetCryptoStream(crypto_stream);

    // Set HANDSHAKE_DONE frame handler callback (returns bool)
    frame_processor_->SetHandshakeDoneCallback(
        [this](auto a) { return HandleHandshakeDoneFrame(a); });
}

ServerConnection::~ServerConnection() {
    // Clean up qlog trace
    if (qlog_trace_) {
        // Use connection ID hash as trace identifier
        std::string trace_id = std::to_string(cid_coordinator_->GetLocalConnectionIDManager()->GetCurrentID().Hash());
        common::QlogManager::Instance().RemoveTrace(trace_id);
    }
}

void ServerConnection::AddRemoteConnectionId(ConnectionID& id) {
    cid_coordinator_->GetRemoteConnectionIDManager()->AddID(id);

    // Create qlog trace for this connection (if not already created)
    if (!qlog_trace_ && common::QlogManager::Instance().IsEnabled()) {
        // Use connection ID hash as trace identifier
        std::string trace_id = std::to_string(cid_coordinator_->GetLocalConnectionIDManager()->GetCurrentID().Hash());
        qlog_trace_ = common::QlogManager::Instance().CreateTrace(trace_id, common::VantagePoint::kServer);

        // Log connection_started event
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

        QLOG_CONNECTION_STARTED(qlog_trace_, data);

        connection_crypto_.SetQlogTrace(qlog_trace_);
        send_manager_.SetQlogTrace(qlog_trace_);
        if (stream_manager_) {
            stream_manager_->SetQlogTrace(qlog_trace_);
        }
        if (frame_processor_) {
            frame_processor_->SetQlogTrace(qlog_trace_);
        }
        if (cid_coordinator_) {
            cid_coordinator_->SetQlogTrace(qlog_trace_);
        }
    }
}

bool ServerConnection::HandleHandshakeDoneFrame(std::shared_ptr<IFrame> frame) {
    // RFC 9000 §19.20: "A server MUST treat receipt of a HANDSHAKE_DONE
    // frame as a connection error of type PROTOCOL_VIOLATION."
    LOG_ERROR("Server received HANDSHAKE_DONE frame from client - PROTOCOL_VIOLATION");
    InnerConnectionClose(QuicErrorCode::kProtocolViolation,
        static_cast<uint16_t>(FrameType::kHandshakeDone),
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

void ServerConnection::WriteCryptoData(std::shared_ptr<IBufferRead> buffer, int32_t err, uint16_t encryption_level) {
    if (err != 0) {
        LOG_ERROR("get crypto data failed. err:%d", err);
        return;
    }

    // Pass buffer memory to BoringSSL via VisitData, then advance read pointer
    // to consume the data. This prevents re-processing on subsequent calls.
    uint32_t total_consumed = 0;
    bool process_ok = true;
    buffer->VisitData([&](uint8_t* data, uint32_t len) -> bool {
        if (!tls_connection_->ProcessCryptoData(data, len, encryption_level)) {
            LOG_ERROR("process crypto data failed. err:%d", err);
            process_ok = false;
            return false;  // stop visiting
        }
        total_consumed += len;
        return true;  // continue to next segment
    });

    // Advance read pointer to consume processed data, preventing duplicate
    // processing on subsequent recv_cb_ invocations
    if (total_consumed > 0) {
        buffer->MoveReadPt(total_consumed);
    }

    if (!process_ok) {
        return;
    }

    if (tls_connection_->DoHandleShake()) {
        LOG_DEBUG("handshake done.");
        // RFC 9000 §19.20 / §4.1.1: "The server MUST send a HANDSHAKE_DONE
        // frame as soon as the handshake is complete." DoHandleShake() above
        // returns true exactly on the BoringSSL state where the server has
        // just consumed the client's Finished — i.e. the handshake is
        // complete from the server's point of view — so emitting the frame
        // here, before any other post-handshake bookkeeping, satisfies "as
        // soon as".
        std::shared_ptr<HandshakeDoneFrame> frame = std::make_shared<HandshakeDoneFrame>();
        ToSendFrame(frame);

        // Mark handshake complete to stop PTO probing
        send_manager_.GetSendControl().SetHandshakeComplete();

        // RFC 9000 Section 4.10: Server discards Initial and Handshake spaces after sending HANDSHAKE_DONE
        recv_control_.DiscardPacketNumberSpace(PacketNumberSpace::kInitialNumberSpace);
        recv_control_.DiscardPacketNumberSpace(PacketNumberSpace::kHandshakeNumberSpace);
        send_manager_.DiscardPacketNumberSpace(PacketNumberSpace::kInitialNumberSpace);
        send_manager_.DiscardPacketNumberSpace(PacketNumberSpace::kHandshakeNumberSpace);
        LOG_INFO("Server: Discarded Initial and Handshake packet number spaces after sending HANDSHAKE_DONE");

        // Log key_discarded events for Initial and Handshake keys
        if (qlog_trace_) {
            common::KeyDiscardedData discard_data;
            discard_data.key_type = "initial";
            discard_data.trigger = "handshake_done";
            QLOG_KEY_DISCARDED(qlog_trace_, discard_data);
            discard_data.key_type = "handshake";
            QLOG_KEY_DISCARDED(qlog_trace_, discard_data);
        }

        state_machine_.OnHandshakeDone();
        // notify handshake done
        if (handshake_done_cb_) {
            handshake_done_cb_(shared_from_this());
        }
    }
}

void ServerConnection::SSLAlpnSelect(
    const unsigned char** out, unsigned char* outlen, const unsigned char* in, unsigned int inlen, void* arg) {
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