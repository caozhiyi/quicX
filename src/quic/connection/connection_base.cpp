
#include <cstring>

#include "common/buffer/buffer_chunk.h"
#include "common/buffer/single_block_buffer.h"
#include "common/log/log.h"
#include "common/metrics/metrics.h"
#include "common/metrics/metrics_std.h"
#include "common/qlog/qlog.h"
#include "common/util/time.h"

#include "quic/common/version.h"
#include "quic/connection/connection_base.h"
#include "quic/connection/error.h"
#include "quic/connection/util.h"
#include "quic/frame/connection_close_frame.h"
#include "quic/frame/type.h"
#include "quic/packet/handshake_packet.h"
#include "quic/packet/init_packet.h"
#include "quic/packet/retry_packet.h"
#include "quic/packet/rtt_0_packet.h"
#include "quic/packet/rtt_1_packet.h"
#include "quic/packet/type.h"
#include "quic/packet/version_negotiation_packet.h"
#include "quic/quicx/global_resource.h"
#include "quic/stream/fix_buffer_frame_visitor.h"

namespace quicx {
namespace quic {

BaseConnection::BaseConnection(StreamIDGenerator::StreamStarter start, bool ecn_enabled,
    std::shared_ptr<ISender> sender, std::shared_ptr<common::IEventLoop> loop,
    std::function<void(std::shared_ptr<IConnection>)> active_connection_cb,
    std::function<void(std::shared_ptr<IConnection>)> handshake_done_cb,
    std::function<void(ConnectionID&, std::shared_ptr<IConnection>)> add_conn_id_cb,
    std::function<void(ConnectionID&)> retire_conn_id_cb,
    std::function<void(std::shared_ptr<IConnection>, uint64_t error, const std::string& reason)> connection_close_cb):
    IConnection(
        sender, active_connection_cb, handshake_done_cb, add_conn_id_cb, retire_conn_id_cb, connection_close_cb),
    ecn_enabled_(ecn_enabled),
    recv_control_(loop->GetTimer()),
    send_manager_(loop->GetTimer()),
    event_loop_(loop),
    last_communicate_time_(0),
    flow_control_(start),
    state_machine_(this) {
    // Metrics: Record handshake start time
    handshake_start_time_ = common::UTCTimeMsec();
    connection_crypto_.SetRemoteTransportParamCB(
        std::bind(&BaseConnection::OnTransportParams, this, std::placeholders::_1));

    // Initialize connection ID coordinator (refactored)
    cid_coordinator_ = std::make_unique<ConnectionIDCoordinator>(event_loop_, send_manager_,
        std::bind(&BaseConnection::AddConnectionId, this, std::placeholders::_1),
        std::bind(&BaseConnection::RetireConnectionId, this, std::placeholders::_1));
    cid_coordinator_->Initialize();

    send_manager_.SetSendRetryCallBack(std::bind(&BaseConnection::ActiveSend, this));
    send_manager_.SetFlowControl(&flow_control_);

    // RFC 9000: Setup immediate ACK callback for Initial/Handshake/out-of-order packets
    recv_control_.SetImmediateAckCB([this](PacketNumberSpace ns) { SendImmediateAckAtLevel(ns); });

    // Setup delayed ACK callback for normal Application packets
    recv_control_.SetActiveSendCB(std::bind(&BaseConnection::ActiveSend, this));

    transport_param_.AddTransportParamListener(
        std::bind(&RecvControl::UpdateConfig, &recv_control_, std::placeholders::_1));
    transport_param_.AddTransportParamListener(
        std::bind(&SendManager::UpdateConfig, &send_manager_, std::placeholders::_1));
    transport_param_.AddTransportParamListener(
        std::bind(&ConnectionFlowControl::UpdateConfig, &flow_control_, std::placeholders::_1));

    // Set stream data ACK callback for tracking stream completion
    send_manager_.send_control_.SetStreamDataAckCallback(std::bind(
        &BaseConnection::OnStreamDataAcked, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    // Initialize timer coordinator (refactored)
    timer_coordinator_ =
        std::make_unique<TimerCoordinator>(event_loop_, transport_param_, send_manager_, state_machine_);

    // Initialize path manager (refactored)
    path_manager_ = std::make_unique<PathManager>(event_loop_, send_manager_, *cid_coordinator_, transport_param_,
        peer_addr_, std::bind(&BaseConnection::ToSendFrame, this, std::placeholders::_1),
        std::bind(&BaseConnection::ActiveSend, this),
        [this](const common::Address& addr) { this->SetPeerAddress(addr); });

    // Initialize stream manager (refactored)
    stream_manager_ = std::make_unique<StreamManager>(event_loop_, flow_control_, transport_param_, send_manager_,
        stream_state_cb_, std::bind(&BaseConnection::ToSendFrame, this, std::placeholders::_1),
        std::bind(&BaseConnection::ActiveSendStream, this, std::placeholders::_1),
        std::bind(&BaseConnection::InnerStreamClose, this, std::placeholders::_1),
        std::bind(&BaseConnection::InnerConnectionClose, this, std::placeholders::_1, std::placeholders::_2,
            std::placeholders::_3));

    // Initialize connection closer (refactored)
    connection_closer_ = std::make_unique<ConnectionCloser>(
        event_loop_, state_machine_, send_manager_, transport_param_, connection_close_cb);

    // Initialize frame processor (refactored)
    frame_processor_ =
        std::make_unique<FrameProcessor>(state_machine_, connection_crypto_, flow_control_, send_manager_,
            *stream_manager_, *cid_coordinator_, *path_manager_, *connection_closer_, transport_param_, token_);
    // Set callbacks for frame processor
    frame_processor_->SetToSendFrameCallback(std::bind(&BaseConnection::ToSendFrame, this, std::placeholders::_1));
    frame_processor_->SetActiveSendCallback(std::bind(&BaseConnection::ActiveSend, this));
    frame_processor_->SetInnerConnectionCloseCallback(std::bind(&BaseConnection::InnerConnectionClose, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    frame_processor_->SetStreamStateCallback(stream_state_cb_);
    frame_processor_->SetRetryPendingStreamRequestsCallback(
        std::bind(&BaseConnection::RetryPendingStreamRequests, this));

    // Metrics: Connection created
    common::Metrics::GaugeInc(common::MetricsStd::QuicConnectionsActive);
    common::Metrics::CounterInc(common::MetricsStd::QuicConnectionsTotal);
}

BaseConnection::~BaseConnection() {
    // Metrics: Connection closed
    common::Metrics::GaugeDec(common::MetricsStd::QuicConnectionsActive);
    common::Metrics::CounterInc(common::MetricsStd::QuicConnectionsClosed);

    // Metrics: Record PTO count per connection
    uint32_t pto_count = send_manager_.GetRttCalculator().GetConsecutivePTOCount();
    common::Metrics::HistogramObserve(common::MetricsStd::PtoCountPerConnection, pto_count);
}

void BaseConnection::Close() {
    if (!event_loop_->IsInLoopThread()) {
        event_loop_->RunInLoop([self = shared_from_this()]() { self->CloseInternal(); });
        return;
    }
    CloseInternal();
}

void BaseConnection::SetActiveConnectionCB(std::function<void(std::shared_ptr<IConnection>)> active_cb) {
    active_connection_cb_ = active_cb;
}

void BaseConnection::CloseInternal() {
    if (state_machine_.GetState() != ConnectionStateType::kStateConnected) {
        common::LOG_ERROR("BaseConnection::CloseInternal called in invalid state: %d", state_machine_.GetState());
        return;
    }
    common::LOG_INFO("BaseConnection::CloseInternal called");
    send_manager_.ClearActiveStreams();
    // Clear retransmission data to prevent retransmitting packets after close
    send_manager_.ClearRetransmissionData();

    // Delegate to connection closer
    connection_closer_->StartGracefulClose(std::bind(&BaseConnection::ActiveSend, this));
}

void BaseConnection::Reset(uint32_t error_code) {
    ImmediateClose(error_code, 0, "application reset.");
}

std::shared_ptr<IQuicStream> BaseConnection::MakeStream(StreamDirection type) {
    // Delegate to stream manager
    return stream_manager_->MakeStreamWithFlowControl(type);
}

bool BaseConnection::MakeStreamAsync(StreamDirection type, stream_creation_callback callback) {
    // Delegate to stream manager
    return stream_manager_->MakeStreamAsync(type, callback);
}

uint64_t BaseConnection::AddTimer(timer_callback callback, uint32_t timeout_ms) {
    if (!timer_coordinator_) {
        common::LOG_ERROR("BaseConnection::AddTimer: timer_coordinator_ is null");
        return 0;
    }
    return timer_coordinator_->AddTimer(callback, timeout_ms);
}

void BaseConnection::RemoveTimer(uint64_t timer_id) {
    if (!timer_coordinator_) {
        common::LOG_ERROR("BaseConnection::RemoveTimer: timer_coordinator_ is null");
        return;
    }
    timer_coordinator_->RemoveTimer(timer_id);
}

void BaseConnection::RetryPendingStreamRequests() {
    // Delegate to stream manager
    stream_manager_->RetryPendingStreamRequests();
}

void BaseConnection::AddTransportParam(const QuicTransportParams& tp_config) {
    transport_param_.Init(tp_config);

    // set transport param. TODO define tp length
    uint8_t tp_buffer[1024];
    common::BufferWriteView write_buffer(tp_buffer, sizeof(tp_buffer));
    if (!transport_param_.Encode(write_buffer)) {
        common::LOG_ERROR("encode transport param failed");
        return;
    }

    // RFC9001 4.1.3: Before starting the handshake, QUIC provides TLS with the transport parameters (see Section 8.2)
    // that it wishes to carry.
    tls_connection_->AddTransportParam(tp_buffer, write_buffer.GetDataLength());
}

uint64_t BaseConnection::GetConnectionIDHash() {
    return cid_coordinator_->GetConnectionIDHash();
}

bool BaseConnection::GenerateSendData(std::shared_ptr<common::IBuffer> buffer, SendOperation& send_operation) {
    // Check if connection should timeout due to excessive PTOs
    // CheckPTOTimeout();

    // RFC 9000: Draining and Closed states MUST NOT send any packets
    if (state_machine_.GetState() == ConnectionStateType::kStateDraining ||
        state_machine_.GetState() == ConnectionStateType::kStateClosed) {
        send_operation = SendOperation::kAllSendDone;
        return false;
    }

    // RFC 9000: In Closing state, only send CONNECTION_CLOSE frames
    // Don't retransmit other packets or send stream data
    if (state_machine_.GetState() == ConnectionStateType::kStateClosing) {
        // Only allow sending CONNECTION_CLOSE frames that are already in wait_frame_list_
        // Don't generate ACK frames or retransmit other packets
        bool ret = send_manager_.GetSendData(
            buffer, GetCurEncryptionLevel(), connection_crypto_.GetCryptographer(GetCurEncryptionLevel()));
        send_operation = send_manager_.GetSendOperation();
        // In Closing state, if GetSendData returns true but buffer is empty,
        // it means there's no CONNECTION_CLOSE frame to send (wait_frame_list_ is empty)
        // This is valid - we just don't have anything to send right now
        return ret;
    }

    // make quic packet
    uint8_t encrypto_level = GetCurEncryptionLevel();
    auto crypto_grapher = connection_crypto_.GetCryptographer(encrypto_level);
    if (!crypto_grapher) {
        // fallback to Initial keys if available (early handshake bootstrap)
        auto init_crypto = connection_crypto_.GetCryptographer(kInitial);
        if (init_crypto) {
            encrypto_level = kInitial;
            crypto_grapher = init_crypto;
        }
    }
    if (!crypto_grapher) {
        common::LOG_ERROR("encrypt grapher is not ready.");
        return false;
    }

    // PATH_CHALLENGE/PATH_RESPONSE frames can only be sent in 1-RTT packets
    // If we're doing path validation and Application keys are ready, use them
    if (path_manager_->IsPathProbeInflight() && encrypto_level < kApplication) {
        auto app_crypto = connection_crypto_.GetCryptographer(kApplication);
        if (app_crypto) {
            encrypto_level = kApplication;
            crypto_grapher = app_crypto;
        }
    }

    // RFC 9000: Generate ACK frames for ALL packet number spaces with pending ACKs
    // Even after handshake completes, we need to ACK any pending Initial/Handshake packets
    // to prevent the peer from unnecessary retransmissions

    // Try to generate ACKs for Initial and Handshake spaces first
    if (encrypto_level >= kHandshake) {
        // If we're at Handshake or Application level, check if there are pending ACKs for Initial space
        auto init_ack = recv_control_.MayGenerateAckFrame(
            common::UTCTimeMsec(), PacketNumberSpace::kInitialNumberSpace, ecn_enabled_);
        if (init_ack) {
            // We have pending Initial ACKs, but we're past Initial encryption level
            // Need to send them in a different packet or clear them
            // For now, just log a warning - this indicates the issue
            common::LOG_WARN(
                "Pending Initial ACKs exist but cannot send at current encryption level=%d", encrypto_level);
            // TODO: Should send Initial packet with ACK before transitioning
        }
    }

    if (encrypto_level >= kApplication) {
        // If we're at Application level, check for pending Handshake ACKs
        auto hs_ack = recv_control_.MayGenerateAckFrame(
            common::UTCTimeMsec(), PacketNumberSpace::kHandshakeNumberSpace, ecn_enabled_);
        if (hs_ack) {
            common::LOG_WARN(
                "Pending Handshake ACKs exist but cannot send at current encryption level=%d", encrypto_level);
        }
    }

    // Generate ACK for current encryption level's packet number space
    auto ack_frame = recv_control_.MayGenerateAckFrame(
        common::UTCTimeMsec(), CryptoLevel2PacketNumberSpace(encrypto_level), ecn_enabled_);
    if (ack_frame) {
        send_manager_.ToSendFrame(ack_frame);
    }

    bool ret = send_manager_.GetSendData(buffer, encrypto_level, crypto_grapher);
    if (!ret) {
        common::LOG_WARN("there is no data to send.");
    }

    // Mark Initial packet as sent if we just sent one
    if (encrypto_level == kInitial && buffer->GetDataLength() > 0) {
        initial_packet_sent_ = true;
    }

    // Check for graceful close: if all data is sent and graceful close is pending
    send_operation = send_manager_.GetSendOperation();
    if (send_operation == SendOperation::kAllSendDone &&
        state_machine_.GetState() == ConnectionStateType::kStateConnected) {
        // Delegate to connection closer to check if graceful close can complete
        connection_closer_->CheckGracefulCloseComplete(std::bind(&BaseConnection::ActiveSend, this));
    }

    return ret;
}

void BaseConnection::OnPackets(uint64_t now, std::vector<std::shared_ptr<IPacket>>& packets) {
    // RFC 9000 Section 10.2: Handle packets based on connection state

    // Closing state: Check if packet contains CONNECTION_CLOSE, otherwise retransmit
    if (state_machine_.GetState() == ConnectionStateType::kStateClosing) {
        // Try to parse packet to check for CONNECTION_CLOSE frame
        bool has_connection_close = false;
        for (auto& packet : packets) {
            std::shared_ptr<ICryptographer> cryptographer =
                connection_crypto_.GetCryptographer(packet->GetCryptoLevel());
            if (cryptographer) {
                packet->SetCryptographer(cryptographer);

                auto chunk =
                    std::make_shared<common::BufferChunk>(GlobalResource::Instance().GetThreadLocalBlockPool());
                if (!chunk || !chunk->Valid()) {
                    common::LOG_ERROR("failed to allocate decode buffer");
                    continue;
                }

                auto out_plaintext = std::make_shared<common::SingleBlockBuffer>(chunk);
                if (packet->DecodeWithCrypto(out_plaintext)) {
                    // Check if any frame is CONNECTION_CLOSE
                    for (auto& frame : packet->GetFrames()) {
                        if (frame->GetType() == FrameType::kConnectionClose ||
                            frame->GetType() == FrameType::kConnectionCloseApp) {
                            has_connection_close = true;
                            has_connection_close = true;
                            // Enter draining state: peer is also closing
                            state_machine_.OnConnectionCloseFrameReceived();
                            // Clear retransmission data to stop packet loss detection
                            send_manager_.ClearRetransmissionData();
                            common::LOG_DEBUG(
                                "Received CONNECTION_CLOSE while in closing state, entering draining state");
                            break;
                        }
                    }
                } else {
                    // Decryption failed: likely invalid or delayed old packet, ignore
                    common::LOG_DEBUG(
                        "Failed to decrypt packet in closing state (likely delayed old packet), ignoring");
                }
            } else {
                // No cryptographer for this packet level: likely invalid or delayed old packet, ignore
                common::LOG_DEBUG(
                    "No cryptographer for packet level %d in closing state (likely delayed old packet), ignoring",
                    packet->GetCryptoLevel());
            }
            if (has_connection_close) {
                break;
            }
        }

        if (!has_connection_close) {
            // No CONNECTION_CLOSE found, retransmit our CONNECTION_CLOSE
            // Delegate to connection closer to check if retransmission is needed
            uint64_t current_time = (now > 0) ? now : common::UTCTimeMsec();

            if (connection_closer_->ShouldRetransmitConnectionClose(current_time)) {
                common::LOG_DEBUG("Connection in closing state, retransmit CONNECTION_CLOSE");
                auto frame = std::make_shared<ConnectionCloseFrame>();
                frame->SetErrorCode(connection_closer_->GetClosingErrorCode());
                frame->SetErrFrameType(connection_closer_->GetClosingTriggerFrame());
                frame->SetReason(connection_closer_->GetClosingReason());
                send_manager_.ToSendFrame(frame);
                // In Closing state, directly trigger active_connection_cb_ to allow sending CONNECTION_CLOSE
                // ActiveSend() is blocked in Closing state, so we need to call the callback directly
                if (active_connection_cb_) {
                    active_connection_cb_(shared_from_this());
                }
                connection_closer_->MarkConnectionCloseRetransmitted(current_time);
            } else {
                common::LOG_DEBUG("Connection in closing state, skipping retransmit (too soon)");
            }
        }
        return;  // Do not process other frames
    }

    // Draining or Closed state: Discard all packets
    if (state_machine_.GetState() == ConnectionStateType::kStateDraining ||
        state_machine_.GetState() == ConnectionStateType::kStateClosed) {
        common::LOG_DEBUG("Connection in draining/closed state, discard packets");
        return;
    }

    // Normal processing for Connecting/Connected states
    // Accumulate ECN to ACK_ECN counters based on first packet number space
    if (!packets.empty() && ecn_enabled_) {
        auto ns = CryptoLevel2PacketNumberSpace(packets[0]->GetCryptoLevel());
        recv_control_.OnEcnCounters(pending_ecn_, ns);
    }
    for (size_t i = 0; i < packets.size(); i++) {
        common::LOG_DEBUG("get packet. type:%d", packets[i]->GetHeader()->GetPacketType());

        // Process packet (decrypt and decode frames)
        bool packet_processed = false;
        switch (packets[i]->GetHeader()->GetPacketType()) {
            case PacketType::kNegotiationPacketType:
                // Version Negotiation packet - handle specially
                packet_processed = OnVersionNegotiationPacket(packets[i]);
                if (!packet_processed) {
                    common::LOG_ERROR("version negotiation packet handle failed.");
                }
                break;
            case PacketType::kInitialPacketType:
                packet_processed = OnInitialPacket(std::dynamic_pointer_cast<InitPacket>(packets[i]));
                if (!packet_processed) {
                    common::LOG_ERROR("init packet handle failed.");
                }
                break;
            case PacketType::k0RttPacketType:
                packet_processed = On0rttPacket(std::dynamic_pointer_cast<Rtt0Packet>(packets[i]));
                if (!packet_processed) {
                    common::LOG_ERROR("0 rtt packet handle failed.");
                }
                break;
            case PacketType::kHandshakePacketType:
                packet_processed = OnHandshakePacket(std::dynamic_pointer_cast<HandshakePacket>(packets[i]));
                if (!packet_processed) {
                    common::LOG_ERROR("handshakee packet handle failed.");
                }
                break;
            case PacketType::kRetryPacketType:
                packet_processed = OnRetryPacket(std::dynamic_pointer_cast<RetryPacket>(packets[i]));
                if (!packet_processed) {
                    common::LOG_ERROR("retry packet handle failed.");
                }
                break;
            case PacketType::k1RttPacketType:
                packet_processed = On1rttPacket(std::dynamic_pointer_cast<Rtt1Packet>(packets[i]));
                if (!packet_processed) {
                    common::LOG_ERROR("1 rtt packet handle failed.");
                }
                break;
            default:
                common::LOG_ERROR("unknow packet type. type:%d", packets[i]->GetHeader()->GetPacketType());
                break;
        }

        // After processing (decrypting and decoding frames), record packet for ACK tracking
        // At this point, packet->GetFrameTypeBit() should be populated
        if (packet_processed) {
            recv_control_.OnPacketRecv(now, packets[i]);
        }
    }

    // reset idle timeout timer task
    timer_coordinator_->ResetIdleTimer();
}

bool BaseConnection::OnInitialPacket(std::shared_ptr<IPacket> packet) {
    if (!connection_crypto_.InitIsReady()) {
        LongHeader* header = (LongHeader*)packet->GetHeader();
        common::LOG_INFO("Installing Initial Secret for decryption from packet DCID: length=%u",
            header->GetDestinationConnectionIdLength());
        connection_crypto_.InstallInitSecret(
            (uint8_t*)header->GetDestinationConnectionId(), header->GetDestinationConnectionIdLength(), true);
    }
    return OnNormalPacket(packet);
}

bool BaseConnection::On0rttPacket(std::shared_ptr<IPacket> packet) {
    // Handle 0-RTT packet like normal packet using early-data keys if available
    // If early data is disabled on server, the keys won't be available and decryption will fail
    // This is expected behavior - the packet will be dropped and early data will be rejected during TLS handshake
    return OnNormalPacket(packet);
}

bool BaseConnection::On1rttPacket(std::shared_ptr<IPacket> packet) {
    return OnNormalPacket(packet);
}

bool BaseConnection::OnVersionNegotiationPacket(std::shared_ptr<IPacket> packet) {
    // Version Negotiation packets are not encrypted and don't contain frames
    // They are only sent by servers in response to an unsupported version
    // RFC 9000 Section 6: Version Negotiation

    auto vn_packet = std::dynamic_pointer_cast<VersionNegotiationPacket>(packet);
    if (!vn_packet) {
        common::LOG_ERROR("Failed to cast to VersionNegotiationPacket");
        return false;
    }

    auto supported_versions = vn_packet->GetSupportVersion();
    common::LOG_WARN("Received Version Negotiation packet with %zu supported versions", supported_versions.size());
    for (size_t i = 0; i < supported_versions.size(); i++) {
        common::LOG_INFO("  Server supports version: 0x%08x", supported_versions[i]);
    }

    // RFC 9000 Section 6.2: Clients MUST discard Version Negotiation packets
    // that list the QUIC version that was sent in the Initial packet.
    // This is a protection against version downgrade attacks.

    // For now, just log and return true to indicate we handled it
    // In a full implementation, the client should retry with a compatible version
    common::LOG_WARN("Version negotiation not yet implemented - connection will fail");

    // Mark packet as processed so it doesn't cause errors
    return true;
}

bool BaseConnection::OnNormalPacket(std::shared_ptr<IPacket> packet) {
    std::shared_ptr<ICryptographer> cryptographer = connection_crypto_.GetCryptographer(packet->GetCryptoLevel());
    if (!cryptographer) {
        common::LOG_ERROR("decrypt grapher is not ready.");
        return false;
    }

    packet->SetCryptographer(cryptographer);

    auto chunk = std::make_shared<common::BufferChunk>(GlobalResource::Instance().GetThreadLocalBlockPool());
    if (!chunk || !chunk->Valid()) {
        common::LOG_ERROR("failed to allocate decode buffer");
        return false;
    }
    auto out_plaintext = std::make_shared<common::SingleBlockBuffer>(chunk);
    if (!packet->DecodeWithCrypto(out_plaintext)) {
        common::LOG_ERROR("decode packet after decrypt failed.");
        return false;
    }

    // Log packet_received event to qlog
    if (qlog_trace_) {
        common::PacketReceivedData data;
        data.packet_number = packet->GetPacketNumber();
        data.packet_type = packet->GetHeader()->GetPacketType();
        data.packet_size = out_plaintext->GetDataLength();
        // Frame types will be collected in P3 phase
        QLOG_PACKET_RECEIVED(qlog_trace_, data);
    }

    if (!OnFrames(packet->GetFrames(), packet->GetCryptoLevel())) {
        common::LOG_ERROR("process frames failed.");
        return false;
    }
    return true;
}

bool BaseConnection::OnHandshakePacket(std::shared_ptr<IPacket> packet) {
    return OnNormalPacket(packet);
}

bool BaseConnection::OnFrames(std::vector<std::shared_ptr<IFrame>>& frames, uint16_t crypto_level) {
    // Metrics: Frames received
    common::Metrics::CounterInc(common::MetricsStd::FramesRxTotal, frames.size());

    // Update last communicate time for PING frames
    for (size_t i = 0; i < frames.size(); i++) {
        uint16_t type = frames[i]->GetType();
        common::LOG_DEBUG("recv frame: %s", FrameType2String(type).c_str());
        if (type == FrameType::kPing) {
            last_communicate_time_ = common::UTCTimeMsec();
        }
        // Metrics: Connection-level flow control blocked
        if (type == FrameType::kDataBlocked) {
            common::Metrics::CounterInc(common::MetricsStd::QuicFlowControlBlocked);
        }
    }

    // Delegate to frame processor
    return frame_processor_->OnFrames(frames, crypto_level);
}

void BaseConnection::OnTransportParams(TransportParam& remote_tp) {
    transport_param_.Merge(remote_tp);

    // Start idle timeout timer through coordinator
    timer_coordinator_->StartIdleTimer(std::bind(&BaseConnection::OnIdleTimeout, this));

    // Preferred Address Migration (RFC 9000 Section 9.6)
    //
    // IMPORTANT: This is CLIENT-SIDE ONLY logic for handling server's preferred address.
    //
    // How it works:
    // 1. SERVER: Advertises a preferred_address in transport parameters during handshake
    //    - This is typically used when the server wants the client to use a different address
    //    - Example: Load balancer forwards initial connection, server wants client to connect directly
    //    - Server sets this via transport_param.SetPreferredAddress("ip:port") before handshake
    //
    // 2. CLIENT: Receives preferred_address and decides whether to migrate (this code)
    //    - Only if active migration is not disabled
    //    - Only if the preferred address is different from current peer address
    //    - Initiates path validation to the new address
    //    - If validation succeeds, switches to the new address
    //
    // 3. SERVER: Does NOT actively migrate its own address
    //    - Server continues listening on all its addresses
    //    - Server responds to PATH_CHALLENGE from client on the preferred address
    //    - After client validates, communication happens on the new address
    //
    if (!transport_param_.GetDisableActiveMigration()) {
        const auto& pref = transport_param_.GetPreferredAddress();
        if (!pref.empty()) {
            common::LOG_INFO("Server advertised preferred address: %s", pref.c_str());

            // Parse "ip:port" format
            auto pos = pref.find(':');
            if (pos != std::string::npos) {
                common::Address addr(pref.substr(0, pos), static_cast<uint16_t>(std::stoi(pref.substr(pos + 1))));
                if (!(addr == GetPeerAddress())) {
                    common::LOG_INFO("Client initiating migration to server's preferred address: %s:%d",
                        addr.GetIp().c_str(), addr.GetPort());
                    path_manager_->OnObservedPeerAddress(addr);
                } else {
                    common::LOG_DEBUG("Preferred address is same as current address, no migration needed");
                }
            } else {
                common::LOG_WARN("Invalid preferred address format: %s (expected ip:port)", pref.c_str());
            }
        }
    }

    // Initialize local CID pool for potential path migrations
    CheckAndReplenishLocalCIDPool();

    // Start any deferred path probes now that Application keys should be ready
    // (OnTransportParams is called after handshake completes)
    path_manager_->StartNextPathProbe();
}

void BaseConnection::ThreadTransferBefore() {
    // remove idle timeout timer task from old timer (delegated to coordinator)
    timer_coordinator_->OnThreadTransferBefore();
}

void BaseConnection::ThreadTransferAfter() {
    // add idle timeout timer task to new timer (delegated to coordinator)
    timer_coordinator_->OnThreadTransferAfter();
}

void BaseConnection::OnIdleTimeout() {
    // Metrics: Idle timeout
    common::Metrics::CounterInc(common::MetricsStd::IdleTimeoutTotal);

    InnerConnectionClose(QuicErrorCode::kNoError, 0, "idle timeout.");
}

void BaseConnection::OnClosingTimeout() {
    state_machine_.OnCloseTimeout();
}

// RFC 9002: Check for idle timeout from excessive PTOs
void BaseConnection::CheckPTOTimeout() {
    // Only check in Connected state to avoid closing during handshake
    if (state_machine_.GetState() != ConnectionStateType::kStateConnected) {
        return;
    }

    uint32_t consecutive_ptos = send_manager_.GetRttCalculator().GetConsecutivePTOCount();

    // RFC 9002: Close connection after persistent timeout (~3 PTO cycles)
    if (consecutive_ptos >= RttCalculator::kMaxConsecutivePTOs) {
        common::LOG_WARN(
            "Connection idle timeout: %u consecutive PTOs without ACK, closing connection", consecutive_ptos);

        // Metrics: PTO count
        common::Metrics::CounterInc(common::MetricsStd::PtoCountTotal);

        // Close with no error (idle timeout is normal termination)
        InnerConnectionClose(QuicErrorCode::kNoError, 0, "Persistent PTO timeout");
    }
}

void BaseConnection::ToSendFrame(std::shared_ptr<IFrame> frame) {
    send_manager_.ToSendFrame(frame);
    ActiveSend();
}

void BaseConnection::ActiveSendStream(std::shared_ptr<IStream> stream) {
    if (state_machine_.GetState() == ConnectionStateType::kStateClosed ||
        state_machine_.GetState() == ConnectionStateType::kStateDraining ||
        state_machine_.GetState() == ConnectionStateType::kStateClosing) {
        return;
    }
    if (stream->GetStreamID() != 0) {
        has_app_send_pending_ = true;
    }
    send_manager_.ActiveStream(stream);
    ActiveSend();
}

EncryptionLevel BaseConnection::GetCurEncryptionLevel() {
    auto level = connection_crypto_.GetCurEncryptionLevel();

    // In 0-RTT scenario, we need to ensure proper packet sending order:
    // 1. First send Initial packet (with ClientHello)
    // 2. Then send 0-RTT packet (with early data)
    if (has_app_send_pending_ && level == kInitial) {
        // Check if we have 0-RTT keys available
        if (connection_crypto_.GetCryptographer(kEarlyData)) {
            // Check if we have already sent the Initial packet with ClientHello
            // This ensures we don't skip the Initial packet in 0-RTT scenarios
            if (initial_packet_sent_) {
                return kEarlyData;
            } else {
                // Still need to send Initial packet first
                return kInitial;
            }
        }
    }
    return level;
}

void BaseConnection::OnObservedPeerAddress(const common::Address& addr) {
    if (path_manager_) {
        path_manager_->OnObservedPeerAddress(addr);
    }
}

void BaseConnection::ActiveSend() {
    // Don't trigger send retry if connection is closing, draining, or closed
    // This prevents unnecessary retransmissions when connection is terminating
    if (state_machine_.GetState() == ConnectionStateType::kStateClosing ||
        state_machine_.GetState() == ConnectionStateType::kStateDraining ||
        state_machine_.GetState() == ConnectionStateType::kStateClosed) {
        common::LOG_DEBUG(
            "ActiveSend called but connection is in state %d, ignoring", static_cast<int>(state_machine_.GetState()));
        return;
    }

    if (active_connection_cb_) {
        active_connection_cb_(shared_from_this());
    }
}

// RFC 9000: Send ACK packet immediately at specified encryption level
// RFC 9002: ACK frames are not congestion controlled
bool BaseConnection::SendImmediateAckAtLevel(PacketNumberSpace ns) {
    // Determine target encryption level from packet number space
    uint8_t target_level;
    switch (ns) {
        case kInitialNumberSpace:
            target_level = kInitial;
            break;
        case kHandshakeNumberSpace:
            target_level = kHandshake;
            break;
        case kApplicationNumberSpace:
            target_level = kApplication;
            break;
        default:
            common::LOG_WARN("SendImmediateAckAtLevel: invalid packet number space %d", ns);
            return false;
    }

    // Get cryptographer for target level
    auto cryptographer = connection_crypto_.GetCryptographer(target_level);
    if (!cryptographer) {
        // Can't send at this level (keys not available), defer to normal flow
        common::LOG_DEBUG("SendImmediateAckAtLevel: no cryptographer for level %d, falling back", target_level);
        if (active_connection_cb_) {
            active_connection_cb_(shared_from_this());
        }
        return false;
    }

    // Generate ACK frame for this number space
    auto ack_frame = recv_control_.MayGenerateAckFrame(common::UTCTimeMsec(), ns, ecn_enabled_);
    if (!ack_frame) {
        common::LOG_DEBUG("SendImmediateAckAtLevel: no ACK to send for ns=%d", ns);
        return false;
    }

    common::LOG_INFO("Sending immediate ACK for ns=%d at level=%d (bypassing congestion control)", ns, target_level);

    // RFC 9002: ACK-only packets are not congestion controlled
    // Build ACK packet directly without going through normal send path
    // Initial packets need 1200 bytes minimum per RFC 9000, need extra space for headers
    FixBufferFrameVisitor frame_visitor(1400);  // Extra space for packet headers

    // Add the ACK frame
    if (!frame_visitor.HandleFrame(ack_frame)) {
        common::LOG_ERROR("SendImmediateAckAtLevel: failed to add ACK frame to visitor");
        return false;
    }

    // Create packet based on encryption level
    std::shared_ptr<IPacket> packet;
    switch (target_level) {
        case kInitial:
            packet = std::make_shared<InitPacket>();
            break;
        case kHandshake:
            packet = std::make_shared<HandshakePacket>();
            break;
        case kApplication:
            packet = std::make_shared<Rtt1Packet>();
            break;
        case kEarlyData:
            packet = std::make_shared<Rtt0Packet>();
            break;
        default:
            common::LOG_ERROR("SendImmediateAckAtLevel: invalid encryption level %d", target_level);
            return false;
    }

    // Set connection IDs
    auto header = packet->GetHeader();
    if (header->GetHeaderType() == PacketHeaderType::kLongHeader) {
        auto cid = cid_coordinator_->GetLocalConnectionIDManager()->GetCurrentID();
        ((LongHeader*)header)->SetSourceConnectionId(cid.GetID(), cid.GetLength());
        ((LongHeader*)header)->SetVersion(kQuicVersions[0]);
    }
    auto remote_cid = cid_coordinator_->GetRemoteConnectionIDManager()->GetCurrentID();
    header->SetDestinationConnectionId(remote_cid.GetID(), remote_cid.GetLength());

    // Set payload and cryptographer
    packet->SetPayload(frame_visitor.GetBuffer()->GetSharedReadableSpan());
    packet->SetCryptographer(cryptographer);

    // Allocate packet number and encode
    uint64_t pkt_number = send_manager_.pakcet_number_.NextPakcetNumber(ns);
    packet->SetPacketNumber(pkt_number);
    header->SetPacketNumberLength(PacketNumber::GetPacketNumberLength(pkt_number));

    // Encode packet into send buffer
    auto chunk = std::make_shared<common::BufferChunk>(GlobalResource::Instance().GetThreadLocalBlockPool());
    if (!chunk || !chunk->Valid()) {
        common::LOG_ERROR("SendImmediateAckAtLevel: failed to allocate buffer");
        return false;
    }
    auto send_buffer = std::make_shared<common::SingleBlockBuffer>(chunk);

    if (!packet->Encode(send_buffer)) {
        common::LOG_ERROR("SendImmediateAckAtLevel: failed to encode ACK packet");
        return false;
    }

    common::LOG_DEBUG(
        "SendImmediateAckAtLevel: encoded ACK packet #%llu, size=%d", pkt_number, send_buffer->GetDataLength());

    // Register packet with send control for ACK tracking (but NOT for congestion control loss tracking)
    // ACK-only packets don't count towards bytes_in_flight
    send_manager_.send_control_.OnPacketSend(common::UTCTimeMsec(), packet, send_buffer->GetDataLength());

    // Send packet immediately via callback
    SendImmediate(send_buffer);

    return true;
}

void BaseConnection::InnerConnectionClose(uint64_t error, uint16_t tigger_frame, std::string reason) {
    if (error != QuicErrorCode::kNoError) {
        // Metrics: Error statistics
        switch (error) {
            case QuicErrorCode::kFlowControlError:
                common::Metrics::CounterInc(common::MetricsStd::ErrorsFlowControl);
                break;
            case QuicErrorCode::kStreamLimitError:
                common::Metrics::CounterInc(common::MetricsStd::ErrorsStreamLimit);
                break;
            case QuicErrorCode::kProtocolViolation:
            case QuicErrorCode::kFrameEncodingError:
            case QuicErrorCode::kTransportParameterError:
            case QuicErrorCode::kConnectionIdLimitError:
                common::Metrics::CounterInc(common::MetricsStd::ErrorsProtocol);
                break;
            case QuicErrorCode::kInternalError:
                common::Metrics::CounterInc(common::MetricsStd::ErrorsInternal);
                break;
            default:
                break;
        }

        ImmediateClose(error, tigger_frame, reason);

    } else {
        Close();
    }
}

void BaseConnection::ImmediateClose(uint64_t error, uint16_t tigger_frame, std::string reason) {
    if (state_machine_.GetState() != ConnectionStateType::kStateConnected) {
        return;
    }

    // Cancel all streams (delegated to stream manager)
    stream_manager_->ResetAllStreams(error);

    // Delegate to connection closer
    connection_closer_->StartImmediateClose(error, tigger_frame, reason, std::bind(&BaseConnection::ActiveSend, this));
}

void BaseConnection::InnerStreamClose(uint64_t stream_id) {
    // Check if stream exists before closing (for metrics)
    auto stream = stream_manager_->FindStream(stream_id);
    if (stream) {
        // Delegate to stream manager
        stream_manager_->CloseStream(stream_id);

        // Metrics: Stream closed
        common::Metrics::GaugeDec(common::MetricsStd::QuicStreamsActive);
        common::Metrics::CounterInc(common::MetricsStd::QuicStreamsClosed);
    }
}

void BaseConnection::OnStreamDataAcked(uint64_t stream_id, uint64_t max_offset, bool has_fin) {
    // Delegate to stream manager
    stream_manager_->OnStreamDataAcked(stream_id, max_offset, has_fin);
}

void BaseConnection::AddConnectionId(ConnectionID& id) {
    if (add_conn_id_cb_) {
        add_conn_id_cb_(id, shared_from_this());
    }
}

void BaseConnection::RetireConnectionId(ConnectionID& id) {
    if (retire_conn_id_cb_) {
        retire_conn_id_cb_(id);
    }
}

void BaseConnection::CheckAndReplenishLocalCIDPool() {
    // Delegate to ConnectionIDCoordinator
    cid_coordinator_->CheckAndReplenishLocalCIDPool();
}

void BaseConnection::OnStateToConnected() {
    // Log connection_state_updated event to qlog
    if (qlog_trace_) {
        common::ConnectionStateUpdatedData data;
        data.old_state = "handshake";
        data.new_state = "connected";

        auto event_data = std::make_unique<common::ConnectionStateUpdatedData>(data);
        QLOG_EVENT(qlog_trace_, common::QlogEvents::kConnectionStateUpdated, std::move(event_data));
    }

    // Metrics: Calculate and record handshake duration
    if (handshake_start_time_ > 0) {
        uint64_t duration_us = (common::UTCTimeMsec() - handshake_start_time_) * 1000;
        common::Metrics::GaugeSet(common::MetricsStd::QuicHandshakeDurationUs, duration_us);
        common::LOG_DEBUG("Handshake completed in %llu microseconds", duration_us);
    }
}

void BaseConnection::OnStateToClosing() {
    // Log connection_state_updated event to qlog
    if (qlog_trace_) {
        common::ConnectionStateUpdatedData data;
        data.old_state = "connected";  // Could be "handshake" if closing early
        data.new_state = "closing";

        auto event_data = std::make_unique<common::ConnectionStateUpdatedData>(data);
        QLOG_EVENT(qlog_trace_, common::QlogEvents::kConnectionStateUpdated, std::move(event_data));
    }

    send_manager_.ClearRetransmissionData();
    send_manager_.ClearActiveStreams();
    send_manager_.wait_frame_list_.clear();

    auto frame = std::make_shared<ConnectionCloseFrame>();
    frame->SetErrorCode(connection_closer_->GetClosingErrorCode());
    frame->SetErrFrameType(connection_closer_->GetClosingTriggerFrame());
    frame->SetReason(connection_closer_->GetClosingReason());

    // Add CONNECTION_CLOSE frame to send queue
    send_manager_.ToSendFrame(frame);

    // Trigger active connection callback to send CONNECTION_CLOSE frame
    // Note: We need to explicitly trigger sending because ActiveSend() is blocked in Closing state
    if (active_connection_cb_) {
        common::LOG_DEBUG("Triggering active connection callback to send CONNECTION_CLOSE frame");
        active_connection_cb_(shared_from_this());
    }

    // Record the time when CONNECTION_CLOSE is first sent
    // RFC 9000 Section 10.2: Retransmit at most once per PTO to avoid flooding
    // This ensures we don't retransmit too frequently when receiving packets
    connection_closer_->MarkConnectionCloseRetransmitted(common::UTCTimeMsec());

    common::TimerTask task(std::bind(&BaseConnection::OnClosingTimeout, this));
    event_loop_->AddTimer(task, connection_closer_->GetCloseWaitTime() * 3, 0);
}

void BaseConnection::OnStateToDraining() {
    // Log connection_state_updated event to qlog
    if (qlog_trace_) {
        common::ConnectionStateUpdatedData data;
        data.old_state = "closing";  // Could be "connected" if peer initiated close
        data.new_state = "draining";

        auto event_data = std::make_unique<common::ConnectionStateUpdatedData>(data);
        QLOG_EVENT(qlog_trace_, common::QlogEvents::kConnectionStateUpdated, std::move(event_data));
    }

    send_manager_.ClearRetransmissionData();
    send_manager_.ClearActiveStreams();
    send_manager_.wait_frame_list_.clear();

    // Immediately notify application layer when entering Draining state
    // This allows the application to cleanup resources and update UI quickly
    // while QUIC layer still waits for the draining timeout per RFC 9000
    connection_closer_->InvokeConnectionCloseCallback(
        shared_from_this(), connection_closer_->GetClosingErrorCode(), connection_closer_->GetClosingReason());

    common::TimerTask task(std::bind(&BaseConnection::OnClosingTimeout, this));
    event_loop_->AddTimer(task, connection_closer_->GetCloseWaitTime() * 3, 0);
}

void BaseConnection::OnStateToClosed() {
    // Log connection_closed event
    if (qlog_trace_) {
        common::ConnectionClosedData data;
        data.error_code = connection_closer_->GetClosingErrorCode();
        data.reason = connection_closer_->GetClosingReason();

        // Determine trigger based on error code
        if (connection_closer_->GetClosingErrorCode() == 0) {
            data.trigger = "clean";
        } else if (connection_closer_->GetClosingTriggerFrame() != 0) {
            data.trigger = "error";
        } else {
            data.trigger = "application";
        }

        QLOG_CONNECTION_CLOSED(qlog_trace_, data);
        qlog_trace_->Flush();  // ensure event is written
    }

    // Stop idle timer through coordinator
    timer_coordinator_->StopIdleTimer();

    // Only invoke callback if it hasn't been called yet
    // (may have been called earlier in OnStateToDraining)
    connection_closer_->InvokeConnectionCloseCallback(shared_from_this(), QuicErrorCode::kNoError, "normal close.");
}

}  // namespace quic
}  // namespace quicx