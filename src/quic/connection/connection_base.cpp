#include <cstring>

#include "common/buffer/buffer_chunk.h"
#include "common/buffer/single_block_buffer.h"
#include "common/log/log.h"
#include "common/metrics/metrics.h"
#include "common/metrics/metrics_std.h"
#include "common/qlog/qlog.h"
#include "common/util/time.h"
#include "common/buffer/buffer_span.h"

#include "quic/common/version.h"
#include "quic/connection/connection_base.h"
#include "quic/connection/error.h"
#include "quic/connection/util.h"
#include "quic/frame/connection_close_frame.h"
#include "quic/frame/ping_frame.h"
#include "quic/frame/type.h"
#include "quic/packet/packet_number.h"
#include "quic/packet/type.h"
#include "quic/packet/version_negotiation_packet.h"
#include "quic/quicx/global_resource.h"
#include "quic/udp/net_packet.h"

namespace quicx {
namespace quic {

BaseConnection::BaseConnection(StreamIDGenerator::StreamStarter start, bool ecn_enabled,
    std::shared_ptr<common::IEventLoop> loop, std::function<void(std::shared_ptr<IConnection>)> active_connection_cb,
    std::function<void(std::shared_ptr<IConnection>)> handshake_done_cb,
    std::function<void(ConnectionID&, std::shared_ptr<IConnection>)> add_conn_id_cb,
    std::function<void(ConnectionID&)> retire_conn_id_cb,
    std::function<void(std::shared_ptr<IConnection>, uint64_t error, const std::string& reason)> connection_close_cb):
    IConnection(active_connection_cb, handshake_done_cb, add_conn_id_cb, retire_conn_id_cb, connection_close_cb),
    ecn_enabled_(ecn_enabled),
    recv_control_(loop->GetTimer()),
    send_manager_(loop->GetTimer()),
    event_loop_(loop),
    last_communicate_time_(0),
    send_flow_controller_(start),
    recv_flow_controller_(),
    state_machine_(this),
    packet_builder_(std::make_unique<PacketBuilder>()) {
    // Metrics: Record handshake start time
    handshake_start_time_ = common::UTCTimeMsec();
    connection_crypto_.SetRemoteTransportParamCB(
        std::bind(&BaseConnection::OnTransportParams, this, std::placeholders::_1));

    // RFC 9000: When 0-RTT write key is installed, trigger early connection callback
    // so the application can start sending data before the handshake completes
    connection_crypto_.SetEarlyDataReadyCB([this]() {
        common::LOG_INFO("0-RTT early data ready, triggering early connection callback");
        if (handshake_done_cb_) {
            handshake_done_cb_(shared_from_this());
        }
    });

    // Initialize connection ID coordinator (refactored)
    cid_coordinator_ = std::make_unique<ConnectionIDCoordinator>(event_loop_, send_manager_,
        std::bind(&BaseConnection::AddConnectionId, this, std::placeholders::_1),
        std::bind(&BaseConnection::RetireConnectionId, this, std::placeholders::_1));
    cid_coordinator_->Initialize();

    send_manager_.SetSendRetryCallBack(std::bind(&BaseConnection::ActiveSend, this));
    send_manager_.SetSendFlowController(&send_flow_controller_);

    // RFC 9002 §6.2.2.1: During handshake, if PTO fires and no ACK-eliciting
    // data to retransmit, send PING to elicit ACK from peer (anti-amplification)
    send_manager_.GetSendControl().SetProbeNeededCallback([this]() {
        common::LOG_INFO("Handshake probe: sending PING frame to elicit ACK");
        auto ping = std::make_shared<PingFrame>();
        ToSendFrame(ping);
    });

    // RFC 9000: Setup immediate ACK callback for Initial/Handshake/out-of-order packets
    recv_control_.SetImmediateAckCB([this](PacketNumberSpace ns) { SendImmediateAck(ns); });

    // Setup delayed ACK callback for normal Application packets
    recv_control_.SetActiveSendCB(std::bind(&BaseConnection::ActiveSend, this));

    transport_param_.AddTransportParamListener(
        std::bind(&RecvControl::UpdateConfig, &recv_control_, std::placeholders::_1));
    transport_param_.AddTransportParamListener(
        std::bind(&SendManager::UpdateConfig, &send_manager_, std::placeholders::_1));
    transport_param_.AddTransportParamListener(
        std::bind(&SendFlowController::UpdateConfig, &send_flow_controller_, std::placeholders::_1));
    transport_param_.AddTransportParamListener(
        std::bind(&RecvFlowController::UpdateConfig, &recv_flow_controller_, std::placeholders::_1));

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

    // Initialize encryption level scheduler (refactored) - centralizes encryption level selection
    encryption_scheduler_ =
        std::make_unique<EncryptionLevelScheduler>(connection_crypto_, recv_control_, *path_manager_);

    // Initialize stream manager (refactored) - uses IConnectionEventSink interface (no callbacks!)
    stream_manager_ = std::make_unique<StreamManager>(
        *this, event_loop_, transport_param_, send_manager_, stream_state_cb_, &send_flow_controller_);

    // Inject stream manager into send manager for stream scheduling
    send_manager_.SetStreamManager(stream_manager_.get());

    // Initialize connection closer (refactored)
    connection_closer_ = std::make_unique<ConnectionCloser>(
        event_loop_, state_machine_, send_manager_, transport_param_, connection_close_cb);

    // Initialize frame processor (refactored) - uses IConnectionEventSink interface (no callbacks!)
    frame_processor_ = std::make_unique<FrameProcessor>(*this, state_machine_, connection_crypto_, send_manager_,
        *stream_manager_, *cid_coordinator_, *path_manager_, *connection_closer_, transport_param_, token_,
        &send_flow_controller_, &recv_flow_controller_);
    // Set application-level callbacks only
    frame_processor_->SetStreamStateCallback(stream_state_cb_);

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

    // Clear stream manager first to prevent callbacks from accessing destroyed objects
    // Streams may still hold callbacks that reference stream_manager_, so we need to
    // ensure stream_manager_ is cleared before other members are destroyed
    stream_manager_.reset();
}

void BaseConnection::SetSender(std::shared_ptr<ISender> sender) {
    sender_ = sender;
    common::LOG_DEBUG("BaseConnection: Sender injected");
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
    if (!state_machine_.CanSendData()) {
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

void BaseConnection::SetStreamStateCallBack(stream_state_callback cb) {
    // Update base class member
    stream_state_cb_ = cb;
    // Also update FrameProcessor's callback so it can notify HTTP/3 layer of new streams
    if (frame_processor_) {
        frame_processor_->SetStreamStateCallback(cb);
    }
    common::LOG_DEBUG(
        "BaseConnection::SetStreamStateCallBack: callback updated in both IConnection and FrameProcessor");
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

bool BaseConnection::IsTerminating() const {
    return state_machine_.IsTerminating();
}

void BaseConnection::RetryPendingStreamRequests() {
    // Delegate to stream manager
    stream_manager_->RetryPendingStreamRequests();
}

void BaseConnection::AddTransportParam(const QuicTransportParams& tp_config) {
    transport_param_.Init(tp_config);

    // set transport param. TODO define tp length
    uint8_t tp_buffer[1024];
    size_t bytes_written = 0;
    common::BufferSpan buffer_span(tp_buffer, sizeof(tp_buffer));
    if (!transport_param_.Encode(buffer_span, bytes_written)) {
        common::LOG_ERROR("encode transport param failed");
        return;
    }

    // RFC9001 4.1.3: Before starting the handshake, QUIC provides TLS with the transport parameters (see Section 8.2)
    // that it wishes to carry.
    tls_connection_->AddTransportParam(tp_buffer, bytes_written);
}

uint64_t BaseConnection::GetConnectionIDHash() {
    return cid_coordinator_->GetConnectionIDHash();
}

void BaseConnection::OnPackets(uint64_t now, std::vector<std::shared_ptr<IPacket>>& packets) {
    // Closing state: Check if packet contains CONNECTION_CLOSE, otherwise retransmit
    if (state_machine_.IsClosing()) {
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
    if (state_machine_.ShouldIgnorePackets()) {
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
                packet_processed = OnInitialPacket(packets[i]);
                if (!packet_processed) {
                    common::LOG_ERROR("init packet handle failed.");
                }
                break;
            case PacketType::k0RttPacketType:
                packet_processed = On0rttPacket(packets[i]);
                if (!packet_processed) {
                    common::LOG_ERROR("0 rtt packet handle failed.");
                }
                break;
            case PacketType::kHandshakePacketType:
                packet_processed = OnHandshakePacket(packets[i]);
                if (!packet_processed) {
                    common::LOG_ERROR("handshake packet handle failed.");
                }
                break;
            case PacketType::kRetryPacketType:
                packet_processed = OnRetryPacket(packets[i]);
                if (!packet_processed) {
                    common::LOG_ERROR("retry packet handle failed.");
                }
                break;
            case PacketType::k1RttPacketType:
                packet_processed = On1rttPacket(packets[i]);
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

        // Use the version from the incoming packet for Initial secret derivation.
        // This is critical: if the peer sends v1 Initial packets, we must use v1 salt,
        // not our default preferred version (which may be v2).
        uint32_t pkt_version = header->GetVersion();
        if (pkt_version != 0 && pkt_version != quic_version_) {
            common::LOG_INFO("Updating connection version from packet: 0x%08x -> 0x%08x",
                quic_version_, pkt_version);
            SetVersion(pkt_version);
        }

        common::LOG_INFO("Installing Initial Secret for decryption from packet DCID: length=%u, version=0x%08x",
            header->GetDestinationConnectionIdLength(), quic_version_);
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

    // Log all supported versions
    for (size_t i = 0; i < supported_versions.size(); i++) {
        common::LOG_INFO(
            "  Server supports version: 0x%08x (%s)", supported_versions[i], VersionToString(supported_versions[i]));
    }

    // RFC 9000 Section 6.2: Version Negotiation Validation
    // A client MUST discard a Version Negotiation packet that lists
    // the QUIC version from the original Initial packet.
    // This prevents downgrade attacks.

    // Check if our current version is in the list (which would indicate an attack)
    uint32_t our_version = quic_version_;
    bool our_version_in_list = false;

    for (auto version : supported_versions) {
        if (version == our_version) {
            // RFC 9000: If our version is listed, this is likely an attack or error
            // The server should have accepted our Initial packet, not sent VN
            our_version_in_list = true;
            common::LOG_WARN("Version Negotiation lists our current version 0x%08x - possible attack!", our_version);
        }
    }

    // If our version is in the list, discard the VN packet (per RFC 9000)
    if (our_version_in_list) {
        common::LOG_WARN("Discarding Version Negotiation packet - our version was listed (possible downgrade attack)");
        // Return true to indicate packet was handled (but we're ignoring it)
        return true;
    }

    // RFC 9000 Section 6: Version negotiation should only happen once
    // If we've already performed version negotiation and receive another VN packet,
    // this indicates a protocol error or infinite loop - close the connection
    if (version_negotiation_done_) {
        common::LOG_ERROR("Received Version Negotiation packet after already negotiating version - closing connection");
        InnerConnectionClose(QuicErrorCode::kProtocolViolation, 0, "Version negotiation attempted multiple times");
        return true;
    }

    // Select a compatible version using our preference order
    uint32_t compatible_version = SelectVersion(supported_versions);

    // Check if we found a compatible version
    if (compatible_version != 0 && compatible_version != our_version) {
        common::LOG_INFO("Found compatible version: 0x%08x (%s), will reconnect", compatible_version,
            VersionToString(compatible_version));

        // Store the negotiated version
        negotiated_version_ = compatible_version;
        version_negotiation_needed_ = true;

        // Trigger version negotiation callback if set
        if (version_negotiation_cb_) {
            version_negotiation_cb_(compatible_version);
        } else {
            // No callback - just close with error
            common::LOG_WARN("Version negotiation callback not set, closing connection");
            InnerConnectionClose(
                QuicErrorCode::kVersionNegotiationError, 0, "Version negotiation required but no handler");
        }
    } else {
        // No compatible version found
        common::LOG_ERROR("No compatible QUIC version found in server's list");
        InnerConnectionClose(QuicErrorCode::kVersionNegotiationError, 0, "No compatible QUIC version");
    }

    // Metrics: Version negotiation event
    common::Metrics::CounterInc(common::MetricsStd::VersionNegotiationTotal);

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
        data.packet_size = packet->GetSrcBuffer().GetLength();

        // Populate frame types
        auto& frames = packet->GetFrames();
        for (const auto& frame : frames) {
            data.frames.push_back(static_cast<FrameType>(frame->GetType()));
        }

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
    // send_flow_controller_.UpdateConfig(remote_tp);
    send_flow_controller_.UpdateConfig(remote_tp);

    // Remember remote transport params for 0-RTT session caching (RFC 9000 Section 7.4.1)
    has_remote_tp_ = true;
    remote_initial_max_data_ = remote_tp.GetInitialMaxData();
    remote_initial_max_streams_bidi_ = remote_tp.GetInitialMaxStreamsBidi();
    remote_initial_max_streams_uni_ = remote_tp.GetInitialMaxStreamsUni();
    remote_initial_max_stream_data_bidi_local_ = remote_tp.GetInitialMaxStreamDataBidiLocal();
    remote_initial_max_stream_data_bidi_remote_ = remote_tp.GetInitialMaxStreamDataBidiRemote();
    remote_initial_max_stream_data_uni_ = remote_tp.GetInitialMaxStreamDataUni();
    remote_active_connection_id_limit_ = remote_tp.GetActiveConnectionIdLimit();

    // Update peer's active connection ID limit in coordinator
    cid_coordinator_->SetPeerActiveConnectionIDLimit(remote_tp.GetActiveConnectionIdLimit());
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
    if (!state_machine_.CanSendData()) {
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
    if (state_machine_.IsTerminating()) {
        return;
    }
    // Guard against accessing stream_manager_ after destruction
    if (!stream_manager_) {
        return;
    }
    if (stream->GetStreamID() != 0) {
        has_app_send_pending_ = true;
        // Notify scheduler that early data (0-RTT) might be needed
        encryption_scheduler_->SetEarlyDataPending(true);
    }
    // Use StreamManager for stream scheduling (Week 4 refactoring)
    stream_manager_->MarkStreamActive(stream);
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
    if (state_machine_.IsTerminating()) {
        common::LOG_DEBUG("ActiveSend called but connection is terminating, ignoring");
        return;
    }

    if (active_connection_cb_) {
        active_connection_cb_(shared_from_this());
    }
}

// ==================== IConnectionEventSink Implementation ====================
// These methods replace callback-based event notification with direct method calls,
// reducing std::bind overhead and improving performance.

void BaseConnection::OnStreamDataReady(std::shared_ptr<IStream> stream) {
    // Delegate to existing ActiveSendStream method
    ActiveSendStream(stream);
}

void BaseConnection::OnFrameReady(std::shared_ptr<IFrame> frame) {
    // Delegate to existing ToSendFrame method
    ToSendFrame(frame);
}

void BaseConnection::OnConnectionActive() {
    // Delegate to existing ActiveSend method
    ActiveSend();
}

void BaseConnection::OnStreamClosed(uint64_t stream_id) {
    // Delegate to existing InnerStreamClose method
    InnerStreamClose(stream_id);
}

void BaseConnection::OnConnectionClose(uint64_t error, uint16_t frame_type, const std::string& reason) {
    // Delegate to existing InnerConnectionClose method
    InnerConnectionClose(error, frame_type, reason);
}

// ==================== End of IConnectionEventSink Implementation ====================

// Immediate send for critical frames (ACK, PATH_CHALLENGE/RESPONSE, CONNECTION_CLOSE)
// Bypasses normal send path for low latency
bool BaseConnection::SendImmediate(std::shared_ptr<common::IBuffer> buffer) {
    if (!buffer || buffer->GetDataLength() == 0) {
        common::LOG_WARN("SendImmediate: empty buffer");
        return false;
    }

    // Prefer sender_ (direct UDP send) if available
    if (sender_) {
        auto net_packet = std::make_shared<NetPacket>();
        net_packet->SetData(buffer);
        net_packet->SetSocket(sockfd_);
        net_packet->SetAddress(peer_addr_);
        net_packet->SetTime(common::UTCTimeMsec());

        bool result = sender_->Send(net_packet);
        if (result) {
            common::LOG_DEBUG("SendImmediate: packet sent via sender_, size=%d", buffer->GetDataLength());
        } else {
            common::LOG_ERROR("SendImmediate: sender_->Send() failed");
        }
        return result;

    } else {
        common::LOG_ERROR("SendImmediate: no sender_ available");
        return false;
    }
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
    if (!state_machine_.CanSendData()) {
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

bool BaseConnection::InitiateMigration() {
    // RFC 9000 Section 9: Connection Migration (Simple API for interop tests)
    // This is a convenience wrapper that delegates to the production API.
    // It keeps the same local IP but gets a new ephemeral port from the system.

    common::LOG_INFO("InitiateMigration: delegating to production API InitiateMigrationTo()");

    // Get current local address
    std::string current_ip;
    uint32_t current_port;
    GetLocalAddr(current_ip, current_port);

    if (current_ip.empty()) {
        common::LOG_WARN("InitiateMigration: failed to get current local address, using 0.0.0.0");
        current_ip = "0.0.0.0";
    }

    // Delegate to production API: same IP, but port=0 means system chooses new port
    // This creates a real socket switch, which is what production migration does
    MigrationResult result = InitiateMigrationTo(current_ip, 0);

    bool success = (result == MigrationResult::kSuccess);
    if (!success) {
        common::LOG_WARN("InitiateMigration: failed with result %d", static_cast<int>(result));
    }

    return success;
}

MigrationResult BaseConnection::InitiateMigrationTo(const std::string& local_ip, uint16_t local_port) {
    // RFC 9000 Section 9: Connection Migration (Production API)
    // This implements full client-initiated connection migration with local address change

    common::LOG_INFO("BaseConnection::InitiateMigrationTo: starting migration to %s:%d", local_ip.c_str(), local_port);

    // 1. Check if connection is in a state that allows migration
    if (!state_machine_.CanSendData()) {
        common::LOG_WARN("InitiateMigrationTo: connection not in connected state");
        return MigrationResult::kFailedInvalidState;
    }

    // 2. Setup callbacks for socket management
    if (path_manager_) {
        path_manager_->SetSocketCallbacks(
            [this]() { return sockfd_; }, [this](int32_t sock) { migration_sockfd_ = sock; });

        // Set migration complete callback to handle socket switch
        path_manager_->SetMigrationCompleteCallback([this](const MigrationInfo& info) { OnMigrationComplete(info); });
    }

    // 3. Create address and delegate to PathManager
    common::Address local_addr(local_ip, local_port);

    if (!path_manager_) {
        return MigrationResult::kFailedInvalidState;
    }

    return path_manager_->InitiateMigrationToAddress(local_addr);
}

void BaseConnection::SetMigrationCallback(migration_callback cb) {
    migration_cb_ = cb;
}

void BaseConnection::GetLocalAddr(std::string& addr, uint32_t& port) {
    // Use IConnection's implementation which queries from socket
    IConnection::GetLocalAddr(addr, port);
}

bool BaseConnection::IsMigrationSupported() const {
    return !transport_param_.GetDisableActiveMigration();
}

bool BaseConnection::IsMigrationInProgress() const {
    return path_manager_ && path_manager_->IsPathProbeInflight();
}

void BaseConnection::OnMigrationComplete(const MigrationInfo& info) {
    common::LOG_INFO("BaseConnection::OnMigrationComplete: result=%d, is_nat_rebinding=%d",
        static_cast<int>(info.result_), info.is_nat_rebinding_);

    if (info.result_ == MigrationResult::kSuccess) {
        // Migration successful: switch to the new socket
        if (migration_sockfd_ > 0) {
            int32_t old_sock = sockfd_;
            sockfd_ = migration_sockfd_;
            migration_sockfd_ = -1;

            // Update cached local address
            common::Address new_local;
            if (GetLocalAddressFromSocket(sockfd_, new_local)) {
                local_addr_ = new_local;
            }

            common::LOG_INFO("BaseConnection: switched to migration socket %d (old: %d)", sockfd_, old_sock);

            // Note: The old socket might still be in use by the event loop
            // We don't close it here - the caller (Worker) should manage socket lifecycle
        }
    } else {
        // Migration failed: cleanup migration socket if any
        if (migration_sockfd_ > 0) {
            close(migration_sockfd_);
            migration_sockfd_ = -1;
        }
    }

    // Notify application layer
    if (migration_cb_) {
        // Need to cast shared_from_this() to IQuicConnection
        auto self = std::dynamic_pointer_cast<IQuicConnection>(shared_from_this());
        migration_cb_(self, info);
    }
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

// ==================== New High-Level Send Interfaces Implementation ====================

bool BaseConnection::TrySend() {
    // 1. State check - allow Connecting, Connected, and Closing states
    // - Connecting: needed for handshake packets
    // - Connected: normal data transmission
    // - Closing: needed to send CONNECTION_CLOSE frames
    // - Draining/Closed: should not send any packets
    if (state_machine_.IsClosed() || state_machine_.IsDraining()) {
        common::LOG_DEBUG(
            "BaseConnection::TrySend: connection is closed/draining, state=%d", state_machine_.GetState());
        return false;
    }

    // 1.5 RFC 9000 §13.3: Retransmit lost packets first
    // QUIC does not retransmit lost packets directly. Instead, the lost packet
    // (which still holds its original payload/frames) is re-encoded with a new
    // packet number and re-encrypted, then sent as a brand-new packet.
    auto& send_control = send_manager_.GetSendControl();
    if (send_control.NeedReSend()) {
        auto& lost_packets = send_control.GetLostPacket();
        auto lost_pkt = lost_packets.front();
        lost_packets.pop_front();

        // Determine encryption level and get cryptographer
        auto crypto_level = lost_pkt->GetCryptoLevel();
        auto cryptographer = connection_crypto_.GetCryptographer(crypto_level);
        if (!cryptographer) {
            common::LOG_WARN("BaseConnection::TrySend: no cryptographer for lost packet level=%d, dropping", crypto_level);
            return !lost_packets.empty();  // try next lost packet
        }

        // Check congestion window before retransmitting
        uint32_t max_bytes = send_manager_.GetAvailableWindow();
        if (max_bytes == 0) {
            // Put the packet back for later retransmission
            lost_packets.push_front(lost_pkt);
            send_manager_.SetCwndLimited();
            return false;
        }

        // Assign new packet number
        auto ns = CryptoLevel2PacketNumberSpace(crypto_level);
        uint64_t new_pn = send_manager_.GetPacketNumber().NextPakcetNumber(ns);
        lost_pkt->SetPacketNumber(new_pn);
        lost_pkt->GetHeader()->SetPacketNumberLength(PacketNumber::GetPacketNumberLength(new_pn));
        lost_pkt->SetCryptographer(cryptographer);

        // Re-encode with new packet number and fresh encryption
        auto chunk = std::make_shared<common::BufferChunk>(quic::GlobalResource::Instance().GetThreadLocalBlockPool());
        if (!chunk || !chunk->Valid()) {
            common::LOG_ERROR("BaseConnection::TrySend: failed to allocate buffer for retransmission");
            return false;
        }
        auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);

        if (!lost_pkt->Encode(buffer)) {
            common::LOG_ERROR("BaseConnection::TrySend: failed to re-encode lost packet pn=%llu", new_pn);
            return false;
        }

        uint32_t encoded_size = buffer->GetDataLength();

        // Record this retransmission in SendControl so it is tracked for ACK/loss
        send_control.OnPacketSend(common::UTCTimeMsec(), lost_pkt, encoded_size);

        common::LOG_INFO("BaseConnection::TrySend: retransmitted lost packet with new pn=%llu, size=%u", new_pn, encoded_size);

        return SendBuffer(buffer);
    }

    // 2. Get send context (determine encryption level)
    auto send_ctx = encryption_scheduler_->GetNextSendContext();
    common::LOG_DEBUG("BaseConnection::TrySend: selected encryption level=%d", send_ctx.level);

    // 3. Get cryptographer
    auto cryptographer = connection_crypto_.GetCryptographer(send_ctx.level);
    if (!cryptographer) {
        common::LOG_ERROR("BaseConnection::TrySend: no cryptographer for level=%d", send_ctx.level);
        return false;
    }

    // 4. Check congestion window
    uint32_t max_bytes = send_manager_.GetAvailableWindow();
    if (max_bytes == 0) {
        common::LOG_DEBUG("BaseConnection::TrySend: congestion window full");
        // Mark as cwnd limited so that when ACK arrives, send_retry_cb_ will be called
        send_manager_.SetCwndLimited();
        return false;
    }

    // 5. Get pending frames
    auto frames = send_manager_.GetPendingFrames(send_ctx.level, max_bytes);
    common::LOG_DEBUG("BaseConnection::TrySend: got %zu pending frames", frames.size());

    // 6. Add pending ACK if needed
    if (send_ctx.has_pending_ack) {
        auto ack_frame = recv_control_.MayGenerateAckFrame(common::UTCTimeMsec(), send_ctx.ack_space, ecn_enabled_);
        if (ack_frame) {
            frames.insert(frames.begin(), ack_frame);
            common::LOG_DEBUG("BaseConnection::TrySend: added ACK frame for ns=%d", send_ctx.ack_space);
        }
    }

    // 7. Check if there's stream data to send
    bool has_stream_data = send_manager_.HasStreamData(send_ctx.level);
    common::LOG_DEBUG("BaseConnection::TrySend: has_stream_data=%d", has_stream_data);

    // 8. If no data at all, return
    if (frames.empty() && !has_stream_data) {
        common::LOG_DEBUG("BaseConnection::TrySend: no data to send");
        return false;
    }

    // 9. Build data packet context
    PacketBuilder::DataPacketContext build_ctx;
    build_ctx.level = send_ctx.level;
    build_ctx.cryptographer = cryptographer;
    build_ctx.local_cid_manager = cid_coordinator_->GetLocalConnectionIDManager().get();
    build_ctx.remote_cid_manager = cid_coordinator_->GetRemoteConnectionIDManager().get();
    build_ctx.quic_version = connection_crypto_.GetVersion();
    build_ctx.frames = std::move(frames);
    build_ctx.stream_manager = stream_manager_.get();
    build_ctx.include_stream_data = has_stream_data;
    build_ctx.add_padding = (send_ctx.level == kInitial);
    build_ctx.min_size = 1200;
    build_ctx.token = send_manager_.GetToken();

    // Set connection-level flow control limit for stream data
    uint64_t conn_flow_limit = 0;
    std::shared_ptr<IFrame> blocked_frame;
    if (send_flow_controller_.CanSendData(conn_flow_limit, blocked_frame)) {
        // Use minimum of congestion window and flow control limit
        build_ctx.max_stream_data_size =
            static_cast<uint32_t>(std::min(static_cast<uint64_t>(max_bytes), conn_flow_limit));
    } else {
        // Flow control blocked, don't send stream data
        build_ctx.max_stream_data_size = 0;
        if (blocked_frame) {
            // Queue the DATA_BLOCKED frame
            send_manager_.ToSendFrame(blocked_frame);
        }
    }

    // 10. Build packet - allocate buffer chunk first
    auto chunk = std::make_shared<common::BufferChunk>(quic::GlobalResource::Instance().GetThreadLocalBlockPool());
    if (!chunk || !chunk->Valid()) {
        common::LOG_ERROR("BaseConnection::TrySend: failed to allocate buffer chunk");
        return false;
    }
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);

    auto result = packet_builder_->BuildDataPacket(
        build_ctx, buffer, send_manager_.GetPacketNumber(), send_manager_.GetSendControl());

    if (!result.success) {
        common::LOG_ERROR("BaseConnection::TrySend: failed to build packet: %s", result.error_message.c_str());
        return false;
    }

    common::LOG_DEBUG(
        "BaseConnection::TrySend: built packet pn=%llu, size=%u bytes", result.packet_number, result.packet_size);

    // 11. Mark Initial packet as sent (if needed)
    if (send_ctx.level == kInitial) {
        encryption_scheduler_->SetInitialPacketSent(true);
    }

    // 12. Send buffer
    bool send_success = SendBuffer(buffer);

    // 13. RFC 9001 Section 6: Check if Key Update should be triggered
    if (send_success && send_ctx.level == kApplication && key_update_trigger_.IsEnabled()) {
        if (key_update_trigger_.OnBytesSent(result.packet_size)) {
            // Trigger key update
            if (connection_crypto_.TriggerKeyUpdate()) {
                key_update_trigger_.MarkTriggered();
                key_update_trigger_.Reset();
                common::LOG_INFO("Key Update triggered after sending %u bytes", result.packet_size);
            }
        }
    }

    return send_success;
}

bool BaseConnection::SendBuffer(std::shared_ptr<common::IBuffer> buffer) {
    if (!buffer || buffer->GetDataLength() == 0) {
        common::LOG_WARN("BaseConnection::SendBuffer: empty buffer");
        return false;
    }

    // Use sender_ if available (preferred)
    if (sender_) {
        auto packet = std::make_shared<NetPacket>();
        packet->SetData(buffer);
        packet->SetAddress(AcquireSendAddress());
        packet->SetSocket(sockfd_);

        if (!sender_->Send(packet)) {
            common::LOG_ERROR("BaseConnection::SendBuffer: sender_->Send() failed");
            return false;
        }

        common::LOG_DEBUG("BaseConnection::SendBuffer: sent %u bytes via sender_", buffer->GetDataLength());
        return true;
    }

    common::LOG_ERROR("BaseConnection::SendBuffer: no sender available");
    return false;
}

bool BaseConnection::SendImmediateAck(PacketNumberSpace ns) {
    common::LOG_DEBUG("BaseConnection::SendImmediateAck: ns=%d", ns);

    // 1. Determine encryption level from packet number space
    EncryptionLevel target_level;
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
            common::LOG_WARN("BaseConnection::SendImmediateAck: invalid packet number space %d", ns);
            return false;
    }

    // 2. Get cryptographer
    auto cryptographer = connection_crypto_.GetCryptographer(target_level);
    if (!cryptographer) {
        common::LOG_WARN("BaseConnection::SendImmediateAck: no cryptographer for level=%d", target_level);
        return false;
    }

    // 3. Generate ACK frame
    auto ack_frame = recv_control_.MayGenerateAckFrame(common::UTCTimeMsec(), ns, ecn_enabled_);
    if (!ack_frame) {
        common::LOG_DEBUG("BaseConnection::SendImmediateAck: no ACK to send for ns=%d", ns);
        return false;
    }

    // 4. Use PacketBuilder to build ACK packet - allocate buffer chunk first
    auto chunk = std::make_shared<common::BufferChunk>(quic::GlobalResource::Instance().GetThreadLocalBlockPool());
    if (!chunk || !chunk->Valid()) {
        common::LOG_ERROR("BaseConnection::SendImmediateAck: failed to allocate buffer chunk");
        return false;
    }
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);

    auto result = packet_builder_->BuildAckPacket(target_level, cryptographer, ack_frame,
        cid_coordinator_->GetLocalConnectionIDManager().get(), cid_coordinator_->GetRemoteConnectionIDManager().get(),
        buffer, send_manager_.GetPacketNumber(), send_manager_.GetSendControl(), connection_crypto_.GetVersion());

    if (!result.success) {
        common::LOG_ERROR("BaseConnection::SendImmediateAck: failed to build packet: %s", result.error_message.c_str());
        return false;
    }

    common::LOG_DEBUG("BaseConnection::SendImmediateAck: built ACK packet pn=%llu, size=%u", result.packet_number,
        result.packet_size);

    // 5. Send immediately
    return SendImmediate(buffer);
}

bool BaseConnection::SendImmediateFrame(std::shared_ptr<IFrame> frame, EncryptionLevel level) {
    common::LOG_DEBUG("BaseConnection::SendImmediateFrame: frame_type=%d, level=%d", frame->GetType(), level);

    // 1. Get cryptographer
    auto cryptographer = connection_crypto_.GetCryptographer(level);
    if (!cryptographer) {
        common::LOG_ERROR("BaseConnection::SendImmediateFrame: no cryptographer for level=%d", level);
        return false;
    }

    // 2. Use PacketBuilder to build single-frame packet - allocate buffer chunk first
    auto chunk = std::make_shared<common::BufferChunk>(quic::GlobalResource::Instance().GetThreadLocalBlockPool());
    if (!chunk || !chunk->Valid()) {
        common::LOG_ERROR("BaseConnection::SendImmediateFrame: failed to allocate buffer chunk");
        return false;
    }
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);

    auto result = packet_builder_->BuildImmediatePacket(frame, level, cryptographer,
        cid_coordinator_->GetLocalConnectionIDManager().get(), cid_coordinator_->GetRemoteConnectionIDManager().get(),
        buffer, send_manager_.GetPacketNumber(), send_manager_.GetSendControl(), connection_crypto_.GetVersion());

    if (!result.success) {
        common::LOG_ERROR(
            "BaseConnection::SendImmediateFrame: failed to build packet: %s", result.error_message.c_str());
        return false;
    }

    common::LOG_DEBUG(
        "BaseConnection::SendImmediateFrame: built packet pn=%llu, size=%u", result.packet_number, result.packet_size);

    // 3. Send immediately
    return SendImmediate(buffer);
}

}  // namespace quic
}  // namespace quicx