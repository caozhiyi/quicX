#include <cstring>
#include <vector>

#include "common/log/log.h"
#include "common/qlog/qlog.h"
#include "common/buffer/buffer_span.h"

#include "quic/connection/connection_client.h"
#include "quic/connection/session_cache.h"
#include "quic/crypto/retry_crypto.h"
#include "quic/packet/handshake_packet.h"
#include "quic/packet/retry_packet.h"
#include "quic/stream/crypto_stream.h"

namespace quicx {
namespace quic {

ClientConnection::ClientConnection(std::shared_ptr<TLSCtx> ctx, std::shared_ptr<common::IEventLoop> loop,
    const ConnectionCallbacks& callbacks):
    BaseConnection(StreamIDGenerator::StreamStarter::kClient, false, loop, callbacks) {
    tls_connection_ = std::make_shared<TLSClientConnection>(ctx, &connection_crypto_);
    if (!tls_connection_->Init()) {
        common::LOG_ERROR("tls connection init failed.");
    }

    auto crypto_stream = std::make_shared<CryptoStream>(event_loop_,
        std::bind(&ClientConnection::ActiveSendStream, this, std::placeholders::_1),
        std::bind(&ClientConnection::InnerStreamClose, this, std::placeholders::_1),
        std::bind(&ClientConnection::InnerConnectionClose, this, std::placeholders::_1, std::placeholders::_2,
            std::placeholders::_3));
    crypto_stream->SetCryptoStreamReadCallBack(std::bind(
        &ClientConnection::WriteCryptoData, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    connection_crypto_.SetCryptoStream(crypto_stream);

    // Set HANDSHAKE_DONE frame handler callback
    frame_processor_->SetHandshakeDoneCallback(
        std::bind(&ClientConnection::HandleHandshakeDoneFrame, this, std::placeholders::_1));
}

ClientConnection::~ClientConnection() {
    // Clean up qlog trace
    if (qlog_trace_) {
        // Use connection ID hash as trace identifier
        std::string trace_id = std::to_string(cid_coordinator_->GetRemoteConnectionIDManager()->GetCurrentID().Hash());
        common::QlogManager::Instance().RemoveTrace(trace_id);
    }
}

bool ClientConnection::Dial(const common::Address& addr, const std::string& alpn, const QuicTransportParams& tp_config,
    const std::string& server_name) {
    auto tls_conn = std::dynamic_pointer_cast<TLSClientConnection>(tls_connection_);

    // Setup TLS (ALPN, SNI, transport params)
    if (!DialSetupTLS(tls_conn, addr, alpn, tp_config, server_name)) {
        return false;
    }

    // Try to restore session from cache for 0-RTT
    std::string session_der;
    std::string session_key = server_name.empty() ? GetPeerAddress().AsString() : server_name;
    SessionInfo cached_session_info;
    if (SessionCache::Instance().GetSessionWithInfo(session_key, session_der, cached_session_info)) {
        if (!tls_conn->SetSession(reinterpret_cast<const uint8_t*>(session_der.data()), session_der.size())) {
            common::LOG_ERROR("set session failed. session_der:%s", session_der.c_str());
            return false;
        }
        // RFC 9000 Section 7.4.1: Pre-initialize send flow controller with remembered
        // transport params for 0-RTT. This allows MakeStream to work before the
        // handshake completes and new transport params are received.
        if (cached_session_info.has_transport_params && cached_session_info.early_data_capable) {
            TransportParam remembered_tp;
            QuicTransportParams remembered_qtp;
            remembered_qtp.initial_max_data_ = cached_session_info.initial_max_data;
            remembered_qtp.initial_max_streams_bidi_ = cached_session_info.initial_max_streams_bidi;
            remembered_qtp.initial_max_streams_uni_ = cached_session_info.initial_max_streams_uni;
            remembered_qtp.initial_max_stream_data_bidi_local_ = cached_session_info.initial_max_stream_data_bidi_local;
            remembered_qtp.initial_max_stream_data_bidi_remote_ = cached_session_info.initial_max_stream_data_bidi_remote;
            remembered_qtp.initial_max_stream_data_uni_ = cached_session_info.initial_max_stream_data_uni;
            remembered_tp.Init(remembered_qtp);
            send_flow_controller_.UpdateConfig(remembered_tp);
            common::LOG_INFO("0-RTT: Pre-initialized flow controller with remembered TP: "
                "max_data=%u, max_streams_bidi=%u, max_streams_uni=%u",
                cached_session_info.initial_max_data,
                cached_session_info.initial_max_streams_bidi,
                cached_session_info.initial_max_streams_uni);
        }
    }

    // Generate CID, install secrets, handshake, qlog
    return DialFinalize(tls_conn, addr);
}

bool ClientConnection::Dial(const common::Address& addr, const std::string& alpn,
    const std::string& resumption_session_der, const QuicTransportParams& tp_config, const std::string& server_name) {
    auto tls_conn = std::dynamic_pointer_cast<TLSClientConnection>(tls_connection_);

    // Setup TLS (ALPN, SNI, transport params)
    if (!DialSetupTLS(tls_conn, addr, alpn, tp_config, server_name)) {
        return false;
    }

    // Use explicitly provided resumption session
    if (!tls_conn->SetSession(
            reinterpret_cast<const uint8_t*>(resumption_session_der.data()), resumption_session_der.size())) {
        common::LOG_ERROR("set session failed. session_der:%s", resumption_session_der.c_str());
        return false;
    }

    // Generate CID, install secrets, handshake, qlog
    return DialFinalize(tls_conn, addr);
}

bool ClientConnection::DialSetupTLS(std::shared_ptr<TLSClientConnection> tls_conn,
    const common::Address& addr, const std::string& alpn,
    const QuicTransportParams& tp_config, const std::string& server_name) {
    // RFC 9368 §3: A client that wishes to speak QUIC v2 but remain
    // interoperable with v1-only peers SHOULD start its handshake with a v1
    // Initial packet and rely on the Compatible Version Negotiation mechanism
    // (the version_information transport parameter, id 0x11) to upgrade the
    // connection.  Pin the *on-the-wire* Initial version to v1 whenever the
    // application preferred something newer than v1.  The application's
    // preference is preserved in |preferred_version_| (and surfaced to the
    // server via available_versions), and on successful upgrade the connection
    // will transparently switch to |preferred_version_|.
    if (quic_version_ != kQuicVersion1) {
        common::LOG_INFO("RFC 9368: starting client handshake with v1 Initial "
                         "(preferred=0x%08x, compatible VN will upgrade if peer agrees)",
                         quic_version_);
        SetPreferredVersion(quic_version_);
        SetVersion(kQuicVersion1);
    }

    // Set application protocol
    common::LOG_INFO("ClientConnection::DialSetupTLS: configuring ALPN='%s' (len=%zu), SNI='%s'",
        alpn.c_str(), alpn.size(), server_name.c_str());
    uint8_t* alpn_data = (uint8_t*)alpn.c_str();
    if (!tls_conn->AddAlpn(alpn_data, alpn.size())) {
        common::LOG_ERROR("add alpn failed. alpn:%s", alpn.c_str());
        return false;
    }

    // Save for Retry handling
    saved_alpn_ = alpn;
    saved_server_name_ = server_name;

    // Set server name (SNI) for TLS handshake
    if (!server_name.empty()) {
        if (!tls_conn->SetServerName(server_name)) {
            common::LOG_ERROR("set server name failed. server_name:%s", server_name.c_str());
            return false;
        }
    }

    SetPeerAddress(std::move(addr));

    // RFC 9000: Set initial_source_connection_id in transport parameters
    auto local_scid = cid_coordinator_->GetLocalConnectionIDManager()->GetCurrentID();
    QuicTransportParams updated_tp = tp_config;
    updated_tp.initial_source_connection_id_ =
        std::string(reinterpret_cast<const char*>(local_scid.GetID()), local_scid.GetLength());
    AddTransportParam(updated_tp);

    return true;
}

bool ClientConnection::DialFinalize(std::shared_ptr<TLSClientConnection> tls_conn,
    const common::Address& addr) {
    // Generate connection id
    auto dcid = cid_coordinator_->GetRemoteConnectionIDManager()->Generator();

    // RFC 9000: Save original DCID for Retry handling
    original_dcid_ = dcid;

    // Ensure the generated DCID becomes the current ID
    cid_coordinator_->GetRemoteConnectionIDManager()->UseNextID();

    // Install initial secret using the current DCID
    auto current_dcid = cid_coordinator_->GetRemoteConnectionIDManager()->GetCurrentID();
    connection_crypto_.InstallInitSecret(current_dcid.GetID(), current_dcid.GetLength(), false);

    tls_conn->DoHandleShake();

    // Create qlog trace for this connection
    if (common::QlogManager::Instance().IsEnabled()) {
        std::string trace_id = std::to_string(dcid.Hash());
        qlog_trace_ = common::QlogManager::Instance().CreateTrace(trace_id, common::VantagePoint::kClient);

        common::ConnectionStartedData data;
        data.src_ip = "0.0.0.0";  // Client source determined by OS
        data.src_port = 0;
        data.dst_ip = addr.GetIp();
        data.dst_port = addr.GetPort();
        data.src_cid = std::to_string(cid_coordinator_->GetLocalConnectionIDManager()->GetCurrentID().Hash());
        data.dst_cid = std::to_string(dcid.Hash());
        data.protocol = "QUIC";
        data.ip_version = "ipv4";  // TODO: Add IsIPv6() method to Address class

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

    return true;
}

bool ClientConnection::ExportResumptionSession(std::string& out_session_der) {
    auto client_tls_conn = std::dynamic_pointer_cast<TLSClientConnection>(tls_connection_);
    SessionInfo session_info;
    if (!client_tls_conn->ExportSession(out_session_der, session_info)) {
        common::LOG_ERROR("export session failed.");
        return false;
    }

    return true;
}

bool ClientConnection::OnHandshakePacket(std::shared_ptr<IPacket> packet) {
    auto handshake_packet = std::dynamic_pointer_cast<HandshakePacket>(packet);
    if (!handshake_packet) {
        common::LOG_ERROR("packet type is not handshake packet.");
        return false;
    }

    // client side should update remote connection id here
    auto long_header = static_cast<LongHeader*>(handshake_packet->GetHeader());
    cid_coordinator_->GetRemoteConnectionIDManager()->AddID(
        long_header->GetSourceConnectionId(), long_header->GetSourceConnectionIdLength());
    cid_coordinator_->GetRemoteConnectionIDManager()->UseNextID();
    return OnNormalPacket(packet);
}

bool ClientConnection::HandleHandshakeDoneFrame(std::shared_ptr<IFrame> frame) {
    common::LOG_DEBUG("ClientConnection::HandleHandshakeDoneFrame called");
    state_machine_.OnHandshakeDone();

    // Mark handshake complete to stop PTO probing
    send_manager_.GetSendControl().SetHandshakeComplete();

    // RFC 9000 Section 4.10: Discard Initial and Handshake packet number spaces
    recv_control_.DiscardPacketNumberSpace(PacketNumberSpace::kInitialNumberSpace);
    recv_control_.DiscardPacketNumberSpace(PacketNumberSpace::kHandshakeNumberSpace);
    send_manager_.DiscardPacketNumberSpace(PacketNumberSpace::kInitialNumberSpace);
    send_manager_.DiscardPacketNumberSpace(PacketNumberSpace::kHandshakeNumberSpace);
    common::LOG_INFO("Discarded Initial and Handshake packet number spaces per RFC 9000");

    // Log key_discarded events for Initial and Handshake keys
    if (qlog_trace_) {
        common::KeyDiscardedData discard_data;
        discard_data.key_type = "initial";
        discard_data.trigger = "handshake_done";
        QLOG_KEY_DISCARDED(qlog_trace_, discard_data);
        discard_data.key_type = "handshake";
        QLOG_KEY_DISCARDED(qlog_trace_, discard_data);
    }

    common::LOG_DEBUG("handshake_done_cb_ is %s", handshake_done_cb_ ? "SET" : "NULL");
    if (handshake_done_cb_) {
        common::LOG_DEBUG("Calling handshake_done_cb_");
        handshake_done_cb_(shared_from_this());
        common::LOG_DEBUG("handshake_done_cb_ completed");
    }

    if (SessionCache::Instance().IsEnableSessionCache()) {
        SessionInfo session_info;
        std::string session_der;
        auto client_tls_conn = std::dynamic_pointer_cast<TLSClientConnection>(tls_connection_);
        if (client_tls_conn->ExportSession(session_der, session_info)) {
            // RFC 9000 Section 7.4.1: Save remembered transport params for 0-RTT
            if (has_remote_tp_) {
                session_info.has_transport_params = true;
                session_info.initial_max_data = remote_initial_max_data_;
                session_info.initial_max_streams_bidi = remote_initial_max_streams_bidi_;
                session_info.initial_max_streams_uni = remote_initial_max_streams_uni_;
                session_info.initial_max_stream_data_bidi_local = remote_initial_max_stream_data_bidi_local_;
                session_info.initial_max_stream_data_bidi_remote = remote_initial_max_stream_data_bidi_remote_;
                session_info.initial_max_stream_data_uni = remote_initial_max_stream_data_uni_;
                session_info.active_connection_id_limit = remote_active_connection_id_limit_;
                common::LOG_INFO("Saving remembered TP: max_data=%u, max_streams_bidi=%u, max_streams_uni=%u",
                    remote_initial_max_data_, remote_initial_max_streams_bidi_, remote_initial_max_streams_uni_);
            }
            SessionCache::Instance().StoreSession(session_der, session_info);
        }
    }
    return true;
}

bool ClientConnection::OnRetryPacket(std::shared_ptr<IPacket> packet) {
    auto retry_packet = std::dynamic_pointer_cast<RetryPacket>(packet);
    if (!retry_packet) {
        common::LOG_ERROR("Invalid Retry packet cast");
        return false;
    }

    // RFC 9001 §5.8: Verify Retry Integrity Tag before processing
    // Reconstruct "retry packet without tag" = header bytes + token bytes
    auto long_header = static_cast<LongHeader*>(retry_packet->GetHeader());
    auto header_src = long_header->GetHeaderSrcData();
    auto token_span = retry_packet->GetRetryToken();

    if (header_src.Valid() && token_span.Valid()) {
        // Build retry_packet_without_tag: [header][token]
        size_t header_len = header_src.GetLength();
        size_t token_len = token_span.GetLength();
        std::vector<uint8_t> retry_without_tag(header_len + token_len);
        std::memcpy(retry_without_tag.data(), header_src.GetStart(), header_len);
        std::memcpy(retry_without_tag.data() + header_len, token_span.GetStart(), token_len);

        uint32_t version = long_header->GetVersion();
        if (!RetryCrypto::VerifyRetryIntegrityTag(original_dcid_,
                retry_without_tag.data(), retry_without_tag.size(),
                version, retry_packet->GetRetryIntegrityTag())) {
            common::LOG_WARN("Retry Integrity Tag verification failed, discarding Retry packet");
            return false;
        }
        common::LOG_DEBUG("Retry Integrity Tag verified successfully");
    } else {
        common::LOG_ERROR("Invalid Retry packet: missing header or token data");
        return false;
    }

    // RFC 9000 Section 17.2.5: Extract new server CID from Retry packet
    // (reuse long_header and token_span from integrity tag verification above)
    ConnectionID src_cid(long_header->GetSourceConnectionId(), long_header->GetSourceConnectionIdLength());

    // Update remote connection ID (this is the new server CID)
    cid_coordinator_->GetRemoteConnectionIDManager()->AddID(
        long_header->GetSourceConnectionId(), long_header->GetSourceConnectionIdLength());
    cid_coordinator_->GetRemoteConnectionIDManager()->UseNextID();

    // Update token
    token_ = std::string((char*)token_span.GetStart(), token_span.GetLength());

    common::LOG_INFO("Received Retry packet. New DCID: %llu, Token length: %zu, Original DCID: %llu", src_cid.Hash(),
        token_.length(), original_dcid_.Hash());

    // Pass token to SendManager for subsequent Initial packets
    send_manager_.SetToken(token_);

    // RFC 9000 Section 17.2.5.3: The server validates the original DCID
    // via its own transport parameters. The client just needs to save original_dcid_.

    // Step 1: Reset TLS connection (recreate SSL object to restart handshake from scratch)
    auto tls_conn = std::dynamic_pointer_cast<TLSClientConnection>(tls_connection_);
    if (!tls_conn->Reset(saved_alpn_)) {
        common::LOG_ERROR("Failed to reset TLS connection for Retry");
        return false;
    }
    common::LOG_DEBUG("TLS connection reset successfully");

    // Step 2: Re-encode transport parameters for the new ClientHello
    // NOTE: The client MUST NOT include original_destination_connection_id in its
    // transport parameters. Per RFC 9000 §18.2, this is a server-only parameter.
    // The client saves original_dcid_ internally for:
    //   1. Verifying the Retry Integrity Tag (done above)
    //   2. Validating the server's original_destination_connection_id TP later
    QuicTransportParams updated_tp;
    // Copy current transport parameters (preserve all existing settings)
    updated_tp.max_idle_timeout_ms_ = static_cast<uint32_t>(transport_param_.GetMaxIdleTimeout());
    updated_tp.max_udp_payload_size_ = static_cast<uint32_t>(transport_param_.GetmaxUdpPayloadSize());
    updated_tp.initial_max_data_ = static_cast<uint32_t>(transport_param_.GetInitialMaxData());
    updated_tp.initial_max_stream_data_bidi_local_ = static_cast<uint32_t>(transport_param_.GetInitialMaxStreamDataBidiLocal());
    updated_tp.initial_max_stream_data_bidi_remote_ = static_cast<uint32_t>(transport_param_.GetInitialMaxStreamDataBidiRemote());
    updated_tp.initial_max_stream_data_uni_ = static_cast<uint32_t>(transport_param_.GetInitialMaxStreamDataUni());
    updated_tp.initial_max_streams_bidi_ = static_cast<uint32_t>(transport_param_.GetInitialMaxStreamsBidi());
    updated_tp.initial_max_streams_uni_ = static_cast<uint32_t>(transport_param_.GetInitialMaxStreamsUni());
    updated_tp.ack_delay_exponent_ms_ = static_cast<uint32_t>(transport_param_.GetackDelayExponent());
    updated_tp.max_ack_delay_ms_ = static_cast<uint32_t>(transport_param_.GetMaxAckDelay());
    updated_tp.disable_active_migration_ = transport_param_.GetDisableActiveMigration();
    updated_tp.active_connection_id_limit_ = static_cast<uint32_t>(transport_param_.GetActiveConnectionIdLimit());
    updated_tp.initial_source_connection_id_ = transport_param_.GetInitialSourceConnectionId();

    // Do NOT set original_destination_connection_id_ — it's server-only (RFC 9000 §18.2)

    common::LOG_DEBUG("Re-encoding transport parameters for Retry (without original_destination_connection_id)");

    // Re-initialize transport parameters with updated config
    transport_param_.Init(updated_tp);

    // Encode and set updated transport parameters for TLS
    uint8_t tp_buffer[1024];
    size_t bytes_written = 0;
    common::BufferSpan buffer_span(tp_buffer, sizeof(tp_buffer));
    if (!transport_param_.Encode(buffer_span, bytes_written)) {
        common::LOG_ERROR("Failed to encode updated transport param for Retry");
        return false;
    }

    // Update TLS with new transport parameters (this will be used in the new ClientHello)
    if (!tls_conn->AddTransportParam(tp_buffer, bytes_written)) {
        common::LOG_ERROR("Failed to update TLS transport param for Retry");
        return false;
    }

    common::LOG_DEBUG("Updated TLS transport parameters for Retry (client-side only params)");

    // Step 3: Reset QUIC crypto state BEFORE calling DoHandleShake
    // This must be done before DoHandleShake because BoringSSL needs crypto keys to generate ClientHello

    // Reset Initial packet number space
    send_manager_.ResetInitialPacketNumber();

    // Reset Crypto Stream state (offset=0) since handshake restarts
    auto crypto_stream = connection_crypto_.GetCryptoStream();
    if (crypto_stream) {
        crypto_stream->ResetForRetry();
    }

    // Reset Initial cryptographer keys
    connection_crypto_.Reset();

    // RFC 9000 Section 5.2: The Initial secret is derived from the new Destination Connection ID (Retry's Source CID)
    if (!connection_crypto_.InstallInitSecret(src_cid.GetID(), src_cid.GetLength(), false)) {
        common::LOG_ERROR("Failed to install Initial Secrets for Retry");
        return false;
    }
    common::LOG_INFO("Installed Initial Secrets for Retry using new DCID (hash: %llu)", src_cid.Hash());

    // Step 4: NOW restart TLS handshake - crypto keys are ready, will generate new ClientHello
    if (!tls_conn->DoHandleShake()) {
        // DoHandleShake returns false for WANT_READ which is expected, so don't fail here
        common::LOG_DEBUG("TLS handshake pending (WANT_READ), will continue when socket ready");
    } else {
        common::LOG_DEBUG("TLS handshake initiated successfully");
    }

    common::LOG_INFO("Successfully processed Retry packet and restarted TLS handshake");

    // Mark that we've received a Retry (prevent duplicate Retry processing)
    retry_received_ = true;

    // Step 5: Trigger resend of Initial packet
    // The new ClientHello generated by DoHandleShake() will be in the crypto stream
    // ActiveSendStream will queue it for transmission with the new Token
    if (crypto_stream) {
        ActiveSendStream(crypto_stream);
    } else {
        common::LOG_ERROR("Crypto stream not found during Retry handling");
        return false;
    }

    return true;
}

void ClientConnection::WriteCryptoData(std::shared_ptr<IBufferRead> buffer, int32_t err, uint16_t encryption_level) {
    common::LOG_INFO("ClientConnection::WriteCryptoData called. buffer_len=%d, err=%d, level=%d",
        buffer->GetDataLength(), err, encryption_level);
    if (err != 0) {
        common::LOG_ERROR("get crypto data failed. err:%d", err);
        return;
    }

    // Pass buffer memory to BoringSSL via VisitData, then advance read pointer
    // to consume the data. This prevents re-processing on subsequent calls.
    uint32_t total_consumed = 0;
    bool process_ok = true;
    buffer->VisitData([&](uint8_t* data, uint32_t len) -> bool {
        if (!tls_connection_->ProcessCryptoData(data, len, encryption_level)) {
            common::LOG_ERROR("process crypto data failed. err:%d", err);
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
        common::LOG_DEBUG("handshake done.");
    }
}

}  // namespace quic
}  // namespace quicx