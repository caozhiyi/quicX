
#include <cstring>

#include "common/buffer/buffer_chunk.h"
#include "common/buffer/single_block_buffer.h"
#include "common/log/log.h"
#include "common/util/time.h"

#include "common/metrics/metrics.h"
#include "common/metrics/metrics_std.h"
#include "quic/common/version.h"
#include "quic/connection/connection_base.h"
#include "quic/connection/error.h"
#include "quic/connection/util.h"
#include "quic/frame/connection_close_frame.h"
#include "quic/frame/max_data_frame.h"
#include "quic/frame/max_streams_frame.h"
#include "quic/frame/new_connection_id_frame.h"
#include "quic/frame/new_token_frame.h"
#include "quic/frame/path_challenge_frame.h"
#include "quic/frame/path_response_frame.h"
#include "quic/frame/retire_connection_id_frame.h"
#include "quic/frame/stream_frame.h"
#include "quic/frame/type.h"
#include "quic/packet/handshake_packet.h"
#include "quic/packet/init_packet.h"
#include "quic/packet/retry_packet.h"
#include "quic/packet/rtt_0_packet.h"
#include "quic/packet/rtt_1_packet.h"
#include "quic/packet/type.h"
#include "quic/quicx/global_resource.h"
#include "quic/stream/bidirection_stream.h"
#include "quic/stream/fix_buffer_frame_visitor.h"
#include "quic/stream/recv_stream.h"
#include "quic/stream/send_stream.h"

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
    flow_control_(start),
    state_machine_(this),
    closing_error_code_(0),
    closing_trigger_frame_(0),
    closing_reason_("") {
    // Metrics: Record handshake start time
    handshake_start_time_ = common::UTCTimeMsec();
    connection_crypto_.SetRemoteTransportParamCB(
        std::bind(&BaseConnection::OnTransportParams, this, std::placeholders::_1));

    // remote CID manager: manages CIDs provided by peer for us to use
    // We manually send RETIRE_CONNECTION_ID when switching CIDs, not automatically on retire
    remote_conn_id_manager_ = std::make_shared<ConnectionIDManager>();
    local_conn_id_manager_ =
        std::make_shared<ConnectionIDManager>(std::bind(&BaseConnection::AddConnectionId, this, std::placeholders::_1),
            std::bind(&BaseConnection::RetireConnectionId, this, std::placeholders::_1));

    send_manager_.SetSendRetryCallBack(std::bind(&BaseConnection::ActiveSend, this));
    send_manager_.SetFlowControl(&flow_control_);
    send_manager_.SetRemoteConnectionIDManager(remote_conn_id_manager_);
    send_manager_.SetLocalConnectionIDManager(local_conn_id_manager_);

    // RFC 9000: Setup immediate ACK callback for Initial/Handshake/out-of-order packets
    recv_control_.SetImmediateAckCB([this](PacketNumberSpace ns) { SendImmediateAckAtLevel(ns); });

    // Setup delayed ACK callback for normal Application packets
    recv_control_.SetActiveSendCB(std::bind(&BaseConnection::ActiveSend, this));

    transport_param_.AddTransportParamListener(
        std::bind(&RecvControl::UpdateConfig, &recv_control_, std::placeholders::_1));
    transport_param_.AddTransportParamListener(
        std::bind(&SendManager::UpdateConfig, &send_manager_, std::placeholders::_1));
    transport_param_.AddTransportParamListener(
        std::bind(&FlowControl::UpdateConfig, &flow_control_, std::placeholders::_1));

    // Set stream data ACK callback for tracking stream completion
    send_manager_.send_control_.SetStreamDataAckCallback(std::bind(
        &BaseConnection::OnStreamDataAcked, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

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

void BaseConnection::SetImmediateSendCallback(ImmediateSendCallback cb) {
    immediate_send_cb_ = cb;
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

    // Graceful close: Check if there's pending data to send
    if (send_manager_.GetSendOperation() != SendOperation::kAllSendDone) {
        // Mark for graceful closing, will enter Closing state when data send completes
        common::LOG_DEBUG("Graceful close pending, waiting for data to be sent");
        graceful_closing_pending_ = true;

        // Set a timeout to force close if data doesn't complete in time
        // Use 3×PTO as a reasonable timeout (similar to draining period)
        graceful_close_timer_ = common::TimerTask(std::bind(&BaseConnection::OnGracefulCloseTimeout, this));
        event_loop_->AddTimer(graceful_close_timer_, GetCloseWaitTime() * 3, 0);
        common::LOG_DEBUG("Graceful close timeout set to %u ms", GetCloseWaitTime() * 3);
        return;
    }

    // No pending data, immediately enter Closing state
    // Store error info for retransmission
    closing_error_code_ = QuicErrorCode::kNoError;
    closing_trigger_frame_ = 0;
    closing_reason_ = "";
    last_connection_close_retransmit_time_ = 0;  // Reset retransmit timer

    state_machine_.OnClose();
}

void BaseConnection::Reset(uint32_t error_code) {
    ImmediateClose(error_code, 0, "application reset.");
}

std::shared_ptr<IQuicStream> BaseConnection::MakeStream(StreamDirection type) {
    // check streams limit
    uint64_t stream_id;
    std::shared_ptr<IFrame> frame;
    bool can_make_stream = false;
    if (type == StreamDirection::kSend) {
        can_make_stream = flow_control_.CheckLocalUnidirectionStreamLimit(stream_id, frame);
    } else {
        can_make_stream = flow_control_.CheckLocalBidirectionStreamLimit(stream_id, frame);
    }
    if (frame) {
        ToSendFrame(frame);
    }

    if (!can_make_stream) {
        common::LOG_DEBUG("make stream limited.");
        return nullptr;
    }

    std::shared_ptr<IStream> new_stream;
    if (type == StreamDirection::kSend) {
        new_stream = MakeStream(transport_param_.GetInitialMaxStreamDataUni(), stream_id, StreamDirection::kSend);
    } else {
        new_stream = MakeStream(transport_param_.GetInitialMaxStreamDataBidiLocal(), stream_id, StreamDirection::kBidi);
    }
    streams_map_[stream_id] = new_stream;

    // Metrics: Stream created
    common::Metrics::GaugeInc(common::MetricsStd::QuicStreamsActive);
    common::Metrics::CounterInc(common::MetricsStd::QuicStreamsCreated);

    return new_stream;
}

bool BaseConnection::MakeStreamAsync(StreamDirection type, stream_creation_callback callback) {
    // Try to create stream immediately
    auto stream = MakeStream(type);
    if (stream) {
        // Success, invoke callback immediately
        callback(stream);
        return true;
    }

    // Stream creation blocked, check queue size limit
    std::lock_guard<std::mutex> lock(pending_streams_mutex_);

    // Queue size limit: initial_max_streams_bidi_ (user's requirement)
    uint64_t max_queue_size = (type == StreamDirection::kSend) ? flow_control_.GetLocalUnidirectionStreamLimit()
                                                               : flow_control_.GetLocalBidirectionStreamLimit();

    if (pending_stream_requests_.size() >= max_queue_size) {
        common::LOG_ERROR("MakeStreamAsync: retry queue full (size: %zu, limit: %llu), rejecting request",
            pending_stream_requests_.size(), max_queue_size);
        return false;  // Queue full, reject request
    }

    // Add to retry queue
    pending_stream_requests_.push({type, callback});

    common::LOG_DEBUG("MakeStreamAsync: stream creation blocked, queued for retry (queue size: %zu/%llu)",
        pending_stream_requests_.size(), max_queue_size);

    return true;  // Successfully queued
}

uint64_t BaseConnection::AddTimer(timer_callback callback, uint32_t timeout_ms) {
    if (!event_loop_) {
        common::LOG_ERROR("BaseConnection::AddTimer: event_loop_ is null");
        return 0;
    }
    return event_loop_->AddTimer(callback, timeout_ms);
}

void BaseConnection::RemoveTimer(uint64_t timer_id) {
    if (!event_loop_) {
        common::LOG_ERROR("BaseConnection::RemoveTimer: event_loop_ is null");
        return;
    }
    event_loop_->RemoveTimer(timer_id);
}

void BaseConnection::RetryPendingStreamRequests() {
    std::lock_guard<std::mutex> lock(pending_streams_mutex_);

    if (pending_stream_requests_.empty()) {
        return;
    }

    size_t retry_count = 0;
    size_t success_count = 0;
    size_t initial_size = pending_stream_requests_.size();

    while (!pending_stream_requests_.empty()) {
        auto& request = pending_stream_requests_.front();
        auto stream = MakeStream(request.type);

        if (stream) {
            // Success, invoke callback
            request.callback(stream);
            pending_stream_requests_.pop();
            success_count++;
        } else {
            // Still blocked, stop retrying (wait for next MAX_STREAMS)
            break;
        }
        retry_count++;
    }

    common::LOG_INFO("RetryPendingStreamRequests: retried %zu requests, %zu succeeded, %zu remaining (initial: %zu)",
        retry_count, success_count, pending_stream_requests_.size(), initial_size);
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
    tls_connection_->AddTransportParam(tp_buffer, write_buffer.GetDataLength());
}

uint64_t BaseConnection::GetConnectionIDHash() {
    return local_conn_id_manager_->GetCurrentID().Hash();
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
    if (path_probe_inflight_ && encrypto_level < kApplication) {
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
    if (send_operation == SendOperation::kAllSendDone && graceful_closing_pending_ &&
        state_machine_.GetState() == ConnectionStateType::kStateConnected) {
        common::LOG_DEBUG("All data sent, proceeding with graceful close");
        graceful_closing_pending_ = false;

        // Cancel the graceful close timeout timer since we're completing normally
        event_loop_->RemoveTimer(graceful_close_timer_);

        // Enter Closing state and send CONNECTION_CLOSE
        closing_error_code_ = QuicErrorCode::kNoError;
        closing_trigger_frame_ = 0;
        closing_reason_ = "";
        last_connection_close_retransmit_time_ = 0;  // Reset retransmit timer

        state_machine_.OnClose();
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
            // But limit retransmission rate to avoid flooding (RFC 9000 Section 10.2)
            // Retransmit at most once per PTO to avoid excessive retransmissions
            // Use the 'now' parameter passed to OnPackets for time comparison
            uint64_t current_time = (now > 0) ? now : common::UTCTimeMsec();
            uint32_t pto = send_manager_.GetPTO(0);  // Use 0 for max_ack_delay (conservative)
            if (pto == 0) {
                pto = 100;  // Fallback to 100ms if PTO not available
            }

            // Always retransmit if last_connection_close_retransmit_time_ is 0 (first retransmit)
            // or if enough time has passed since last retransmit
            if (last_connection_close_retransmit_time_ == 0 ||
                (current_time - last_connection_close_retransmit_time_) >= pto) {
                common::LOG_DEBUG(
                    "Connection in closing state, retransmit CONNECTION_CLOSE (last retransmit: %llu ms ago)",
                    last_connection_close_retransmit_time_ == 0
                        ? 0
                        : (current_time - last_connection_close_retransmit_time_));
                auto frame = std::make_shared<ConnectionCloseFrame>();
                frame->SetErrorCode(closing_error_code_);
                frame->SetErrFrameType(closing_trigger_frame_);
                frame->SetReason(closing_reason_);
                send_manager_.ToSendFrame(frame);
                // In Closing state, directly trigger active_connection_cb_ to allow sending CONNECTION_CLOSE
                // ActiveSend() is blocked in Closing state, so we need to call the callback directly
                if (active_connection_cb_) {
                    active_connection_cb_(shared_from_this());
                }
                last_connection_close_retransmit_time_ = current_time;
            } else {
                common::LOG_DEBUG(
                    "Connection in closing state, skipping retransmit (last retransmit: %llu ms ago, PTO: %u ms)",
                    current_time - last_connection_close_retransmit_time_, pto);
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
    event_loop_->RemoveTimer(idle_timeout_task_);
    event_loop_->AddTimer(idle_timeout_task_, transport_param_.GetMaxIdleTimeout(), 0);
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

    for (size_t i = 0; i < frames.size(); i++) {
        uint16_t type = frames[i]->GetType();
        common::LOG_DEBUG("recv frame: %s", FrameType2String(type).c_str());
        switch (type) {
            case FrameType::kPadding:
                // do nothing
                break;
            case FrameType::kPing:
                last_communicate_time_ = common::UTCTimeMsec();
                break;
            case FrameType::kAck:
            case FrameType::kAckEcn:
                if (!OnAckFrame(frames[i], crypto_level)) {
                    return false;
                }
                break;
            case FrameType::kCrypto:
                if (!OnCryptoFrame(frames[i])) {
                    return false;
                }
                break;
            case FrameType::kNewToken:
                if (!OnNewTokenFrame(frames[i])) {
                    return false;
                }
                break;
            case FrameType::kMaxData:
                if (!OnMaxDataFrame(frames[i])) {
                    return false;
                }
                break;
            case FrameType::kMaxStreamsBidirectional:
            case FrameType::kMaxStreamsUnidirectional:
                if (!OnMaxStreamFrame(frames[i])) {
                    return false;
                }
                break;
            case FrameType::kDataBlocked:
                // Metrics: Connection-level flow control blocked
                common::Metrics::CounterInc(common::MetricsStd::QuicFlowControlBlocked);
                if (!OnDataBlockFrame(frames[i])) {
                    return false;
                }
                break;
            case FrameType::kStreamsBlockedBidirectional:
            case FrameType::kStreamsBlockedUnidirectional:
                if (!OnStreamBlockFrame(frames[i])) {
                    return false;
                }
                break;
            case FrameType::kNewConnectionId:
                if (!OnNewConnectionIDFrame(frames[i])) {
                    return false;
                }
                break;
            case FrameType::kRetireConnectionId:
                if (!OnRetireConnectionIDFrame(frames[i])) {
                    return false;
                }
                break;
            case FrameType::kPathChallenge:
                if (!OnPathChallengeFrame(frames[i])) {
                    return false;
                }
                break;
            case FrameType::kPathResponse:
                if (!OnPathResponseFrame(frames[i])) {
                    return false;
                }
                break;
            case FrameType::kConnectionClose:
                if (!OnConnectionCloseFrame(frames[i])) {
                    return false;
                }
                break;
            case FrameType::kConnectionCloseApp:
                if (!OnConnectionCloseAppFrame(frames[i])) {
                    return false;
                }
                break;
            case FrameType::kHandshakeDone:
                if (!OnHandshakeDoneFrame(frames[i])) {
                    return false;
                }
                break;
            // ********** stream frame **********
            case FrameType::kResetStream:
            case FrameType::kStopSending:
            case FrameType::kStreamDataBlocked:
            case FrameType::kMaxStreamData:
                if (!OnStreamFrame(frames[i])) {
                    return false;
                }
                break;
            default:
                if (StreamFrame::IsStreamFrame(type)) {
                    if (!OnStreamFrame(frames[i])) {
                        return false;
                    }
                } else {
                    common::LOG_ERROR("invalid frame type. type:%s", type);
                    return false;
                }
        }
    }
    return true;
}

bool BaseConnection::OnStreamFrame(std::shared_ptr<IFrame> frame) {
    auto stream_frame = std::dynamic_pointer_cast<IStreamFrame>(frame);
    if (!stream_frame) {
        common::LOG_ERROR("invalid stream frame.");
        return false;
    }

    // Allow processing of application data only when encryption is ready; 0-RTT stream frames
    // arrive before handshake confirmation and must be accepted per RFC (subject to anti-replay policy
    // which is handled at TLS/session level). Here we don't gate on connection state.
    common::LOG_DEBUG("process stream data frame. stream id:%d", stream_frame->GetStreamID());
    // find stream
    uint64_t stream_id = stream_frame->GetStreamID();
    auto stream = streams_map_.find(stream_id);
    if (stream != streams_map_.end()) {
        // CRITICAL: Hold a local shared_ptr to prevent use-after-free
        // If the callback calls error_handler_ which removes the stream,
        // this local copy keeps the object alive until we return
        auto stream_ptr = stream->second;
        flow_control_.AddRemoteSendData(stream_ptr->OnFrame(frame));
        return true;
    }

    // check streams limit
    std::shared_ptr<IFrame> send_frame;
    bool can_make_stream = flow_control_.CheckRemoteStreamLimit(stream_id, send_frame);
    if (send_frame) {
        ToSendFrame(send_frame);
    }
    if (!can_make_stream) {
        return false;
    }

    // create new stream
    std::shared_ptr<IStream> new_stream;
    if (StreamIDGenerator::GetStreamDirection(stream_id) == StreamIDGenerator::StreamDirection::kBidirectional) {
        new_stream =
            MakeStream(transport_param_.GetInitialMaxStreamDataBidiRemote(), stream_id, StreamDirection::kBidi);

    } else {
        new_stream = MakeStream(transport_param_.GetInitialMaxStreamDataUni(), stream_id, StreamDirection::kRecv);
    }
    // check peer data limit
    if (!flow_control_.CheckRemoteSendDataLimit(send_frame)) {
        InnerConnectionClose(QuicErrorCode::kFlowControlError, frame->GetType(), "flow control stream data limit.");
        return false;
    }
    if (send_frame) {
        ToSendFrame(send_frame);
    }
    // notify stream state
    if (stream_state_cb_) {
        stream_state_cb_(new_stream, 0);
    }
    // new stream process frame
    streams_map_[stream_id] = new_stream;
    flow_control_.AddRemoteSendData(new_stream->OnFrame(frame));
    return true;
}

bool BaseConnection::OnAckFrame(std::shared_ptr<IFrame> frame, uint16_t crypto_level) {
    auto ns = CryptoLevel2PacketNumberSpace(crypto_level);
    send_manager_.OnPacketAck(ns, frame);
    return true;
}

bool BaseConnection::OnCryptoFrame(std::shared_ptr<IFrame> frame) {
    connection_crypto_.OnCryptoFrame(frame);
    return true;
}

bool BaseConnection::OnNewTokenFrame(std::shared_ptr<IFrame> frame) {
    auto token_frame = std::dynamic_pointer_cast<NewTokenFrame>(frame);
    if (!token_frame) {
        common::LOG_ERROR("invalid new token frame.");
        return false;
    }
    auto data = token_frame->GetToken();
    token_ = std::move(std::string((const char*)data, token_frame->GetTokenLength()));
    return true;
}

bool BaseConnection::OnMaxDataFrame(std::shared_ptr<IFrame> frame) {
    auto max_data_frame = std::dynamic_pointer_cast<MaxDataFrame>(frame);
    if (!max_data_frame) {
        common::LOG_ERROR("invalid max data frame.");
        return false;
    }
    uint64_t max_data_size = max_data_frame->GetMaximumData();
    flow_control_.AddLocalSendDataLimit(max_data_size);
    return true;
}

bool BaseConnection::OnDataBlockFrame(std::shared_ptr<IFrame> frame) {
    std::shared_ptr<IFrame> send_frame;
    flow_control_.CheckRemoteSendDataLimit(send_frame);
    if (send_frame) {
        ToSendFrame(send_frame);
    }
    return true;
}

bool BaseConnection::OnStreamBlockFrame(std::shared_ptr<IFrame> frame) {
    std::shared_ptr<IFrame> send_frame;
    flow_control_.CheckRemoteStreamLimit(0, send_frame);
    if (send_frame) {
        ToSendFrame(send_frame);
        ActiveSend();  // Immediately send MAX_STREAMS to reduce latency

        common::LOG_INFO("Received STREAMS_BLOCKED, sending MAX_STREAMS immediately");
    }
    return true;
}

bool BaseConnection::OnMaxStreamFrame(std::shared_ptr<IFrame> frame) {
    auto stream_block_frame = std::dynamic_pointer_cast<MaxStreamsFrame>(frame);

    uint64_t old_limit = 0;
    uint64_t new_limit = stream_block_frame->GetMaximumStreams();

    if (stream_block_frame->GetType() == FrameType::kMaxStreamsBidirectional) {
        old_limit = flow_control_.GetLocalBidirectionStreamLimit();
        flow_control_.AddLocalBidirectionStreamLimit(new_limit);

        common::LOG_INFO(
            "Received MAX_STREAMS_BIDIRECTIONAL: %llu -> %llu, retrying pending requests", old_limit, new_limit);
    } else {
        old_limit = flow_control_.GetLocalUnidirectionStreamLimit();
        flow_control_.AddLocalUnidirectionStreamLimit(new_limit);

        common::LOG_INFO(
            "Received MAX_STREAMS_UNIDIRECTIONAL: %llu -> %llu, retrying pending requests", old_limit, new_limit);
    }

    // Trigger retry of pending stream creation requests
    RetryPendingStreamRequests();

    return true;
}

bool BaseConnection::OnNewConnectionIDFrame(std::shared_ptr<IFrame> frame) {
    auto new_cid_frame = std::dynamic_pointer_cast<NewConnectionIDFrame>(frame);
    if (!new_cid_frame) {
        common::LOG_ERROR("invalid new connection id frame.");
        return false;
    }

    // If Retire Prior To > 0, we need to retire old CIDs and send RETIRE_CONNECTION_ID
    uint64_t retire_prior_to = new_cid_frame->GetRetirePriorTo();
    if (retire_prior_to > 0) {
        // Send RETIRE_CONNECTION_ID for all CIDs with sequence < retire_prior_to
        // We need to iterate from 0 to retire_prior_to-1
        for (uint64_t seq = 0; seq < retire_prior_to; ++seq) {
            auto retire = std::make_shared<RetireConnectionIDFrame>();
            retire->SetSequenceNumber(seq);
            ToSendFrame(retire);
        }
        // Remove these CIDs from our remote pool
        remote_conn_id_manager_->RetireIDBySequence(retire_prior_to - 1);
    }

    // Add new CID to pool
    ConnectionID id;
    new_cid_frame->GetConnectionID(id);
    remote_conn_id_manager_->AddID(id);
    return true;
}

bool BaseConnection::OnRetireConnectionIDFrame(std::shared_ptr<IFrame> frame) {
    auto retire_cid_frame = std::dynamic_pointer_cast<RetireConnectionIDFrame>(frame);
    if (!retire_cid_frame) {
        common::LOG_ERROR("invalid retire connection id frame.");
        return false;
    }
    // Peer is retiring a CID we provided to them, remove from local pool
    local_conn_id_manager_->RetireIDBySequence(retire_cid_frame->GetSequenceNumber());
    return true;
}

bool BaseConnection::OnConnectionCloseFrame(std::shared_ptr<IFrame> frame) {
    auto close_frame = std::dynamic_pointer_cast<ConnectionCloseFrame>(frame);
    if (!close_frame) {
        common::LOG_ERROR("invalid connection close frame.");
        return false;
    }

    if (state_machine_.GetState() != ConnectionStateType::kStateConnected &&
        state_machine_.GetState() != ConnectionStateType::kStateClosing) {
        return false;
    }

    // Cancel graceful close if it's pending (peer initiated close)
    if (graceful_closing_pending_) {
        common::LOG_DEBUG("Canceling graceful close due to peer CONNECTION_CLOSE");
        graceful_closing_pending_ = false;
        event_loop_->RemoveTimer(graceful_close_timer_);
    }

    // Store error info from peer's CONNECTION_CLOSE for application notification
    closing_error_code_ = close_frame->GetErrorCode();
    closing_trigger_frame_ = close_frame->GetErrFrameType();
    closing_reason_ = close_frame->GetReason();

    // Received CONNECTION_CLOSE from peer: enter Draining state
    // RFC 9000: In draining state, endpoint MUST NOT send any packets
    state_machine_.OnConnectionCloseFrameReceived();

    common::LOG_INFO("Connection entering draining state. error_code:%u, reason:%s", close_frame->GetErrorCode(),
        close_frame->GetReason().c_str());

    return true;
}

bool BaseConnection::OnConnectionCloseAppFrame(std::shared_ptr<IFrame> frame) {
    return OnConnectionCloseFrame(frame);
}

bool BaseConnection::OnPathChallengeFrame(std::shared_ptr<IFrame> frame) {
    auto challenge_frame = std::dynamic_pointer_cast<PathChallengeFrame>(frame);
    if (!challenge_frame) {
        common::LOG_ERROR("invalid path challenge frame.");
        return false;
    }
    auto data = challenge_frame->GetData();
    auto response_frame = std::make_shared<PathResponseFrame>();
    response_frame->SetData(data);
    ToSendFrame(response_frame);
    return true;
}

bool BaseConnection::OnPathResponseFrame(std::shared_ptr<IFrame> frame) {
    auto response_frame = std::dynamic_pointer_cast<PathResponseFrame>(frame);
    if (!response_frame) {
        common::LOG_ERROR("invalid path response frame.");
        return false;
    }
    auto data = response_frame->GetData();
    if (path_probe_inflight_ && memcmp(data, pending_path_challenge_data_, 8) == 0) {
        // token matched: path validated -> promote candidate to active
        path_probe_inflight_ = false;
        event_loop_->RemoveTimer(path_probe_task_);  // Cancel retry timer
        memset(pending_path_challenge_data_, 0, sizeof(pending_path_challenge_data_));

        if (!(candidate_peer_addr_ == peer_addr_)) {
            common::LOG_INFO("Path validated successfully, switching from %s:%d to %s:%d", peer_addr_.GetIp().c_str(),
                peer_addr_.GetPort(), candidate_peer_addr_.GetIp().c_str(), candidate_peer_addr_.GetPort());

            SetPeerAddress(candidate_peer_addr_);
            // Rotate to next remote CID and retire the old one
            auto old_cid = remote_conn_id_manager_->GetCurrentID();
            if (remote_conn_id_manager_->UseNextID()) {
                // Send RETIRE_CONNECTION_ID for the old CID
                auto retire = std::make_shared<RetireConnectionIDFrame>();
                retire->SetSequenceNumber(old_cid.GetSequenceNumber());
                ToSendFrame(retire);
            }
            // reset cwnd/RTT and PMTU for new path
            send_manager_.ResetPathSignals();
            send_manager_.ResetMtuForNewPath();
            // kick off a minimal PMTU probe sequence on the new path
            send_manager_.StartMtuProbe();
            ExitAntiAmplification();
        }

        // candidate consumed
        candidate_peer_addr_ = common::Address();

        // Check and replenish local CID pool after successful migration
        CheckAndReplenishLocalCIDPool();

        // Start probing next address in queue if any
        StartNextPathProbe();
    }
    return true;
}

void BaseConnection::OnTransportParams(TransportParam& remote_tp) {
    transport_param_.Merge(remote_tp);
    idle_timeout_task_.SetTimeoutCallback(std::bind(&BaseConnection::OnIdleTimeout, this));
    // TODO: modify idle timer set point
    event_loop_->AddTimer(idle_timeout_task_, transport_param_.GetMaxIdleTimeout(), 0);

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
                    candidate_peer_addr_ = addr;
                    StartPathValidationProbe();
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
    if (!path_probe_inflight_ && !pending_candidate_addrs_.empty()) {
        common::LOG_INFO("Starting deferred path probe (Application keys now ready)");
        StartNextPathProbe();
    }
}

void BaseConnection::ThreadTransferBefore() {
    // remove idle timeout timer task from old timer
    event_loop_->RemoveTimer(idle_timeout_task_);
}

void BaseConnection::ThreadTransferAfter() {
    // add idle timeout timer task to new timer
    event_loop_->AddTimer(idle_timeout_task_, transport_param_.GetMaxIdleTimeout(), 0);
}

void BaseConnection::OnIdleTimeout() {
    // Metrics: Idle timeout
    common::Metrics::CounterInc(common::MetricsStd::IdleTimeoutTotal);

    InnerConnectionClose(QuicErrorCode::kNoError, 0, "idle timeout.");
}

void BaseConnection::OnClosingTimeout() {
    state_machine_.OnCloseTimeout();
}

void BaseConnection::OnGracefulCloseTimeout() {
    // Graceful close timeout: force entering Closing state even if data hasn't finished sending
    // Graceful close timeout: force entering Closing state even if data hasn't finished sending
    if (graceful_closing_pending_ && state_machine_.GetState() == ConnectionStateType::kStateConnected) {
        common::LOG_WARN("Graceful close timeout, forcing connection close");
        graceful_closing_pending_ = false;

        // Force enter Closing state
        closing_error_code_ = QuicErrorCode::kNoError;
        closing_trigger_frame_ = 0;
        closing_reason_ = "graceful close timeout";
        last_connection_close_retransmit_time_ = 0;  // Reset retransmit timer

        state_machine_.OnClose();
    }
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

uint32_t BaseConnection::GetCloseWaitTime() {
    // RFC 9000: Use PTO (Probe Timeout) for connection close timing
    // PTO = smoothed_rtt + max(4*rttvar, kGranularity) + max_ack_delay
    uint32_t max_ack_delay = transport_param_.GetMaxAckDelay();
    uint32_t pto = send_manager_.GetPTO(max_ack_delay);

    // Ensure minimum timeout of 500ms for close timing
    if (pto < 500000) {  // PTO is in microseconds
        pto = 500000;
    }

    return pto / 1000;  // Convert to milliseconds for timer
}

void BaseConnection::ToSendFrame(std::shared_ptr<IFrame> frame) {
    send_manager_.ToSendFrame(frame);
    ActiveSend();
}

void BaseConnection::StartPathValidationProbe() {
    if (path_probe_inflight_) {
        return;
    }

    // PATH_CHALLENGE can only be sent in 1-RTT packets, so Application keys must be ready
    // If not ready yet, the probe will be triggered later when OnTransportParams completes
    if (!connection_crypto_.GetCryptographer(kApplication)) {
        common::LOG_DEBUG("Path validation deferred: Application keys not ready yet");
        // Don't add to pending queue here - caller (OnObservedPeerAddress) already did that
        return;
    }

    // generate PATH_CHALLENGE
    auto challenge = std::make_shared<PathChallengeFrame>();
    challenge->MakeData();
    memcpy(pending_path_challenge_data_, challenge->GetData(), 8);
    path_probe_inflight_ = true;
    EnterAntiAmplification();
    // reset anti-amplification budget on send manager
    send_manager_.ResetAmpBudget();
    ToSendFrame(challenge);
    probe_retry_count_ = 0;
    probe_retry_delay_ms_ = 1 * 100;  // start with 100ms
    ScheduleProbeRetry();
}

void BaseConnection::StartNextPathProbe() {
    // Check if there are pending addresses to probe
    if (pending_candidate_addrs_.empty()) {
        return;
    }

    // Get next address from queue
    candidate_peer_addr_ = pending_candidate_addrs_.front();
    pending_candidate_addrs_.erase(pending_candidate_addrs_.begin());

    common::LOG_INFO("Starting next path probe from queue to %s:%d (remaining in queue: %zu)",
        candidate_peer_addr_.GetIp().c_str(), candidate_peer_addr_.GetPort(), pending_candidate_addrs_.size());

    StartPathValidationProbe();
}

void BaseConnection::EnterAntiAmplification() {
    // Disable streams while path is unvalidated to limit to probing/ACK frames
    send_manager_.SetStreamsAllowed(false);
}

void BaseConnection::ExitAntiAmplification() {
    send_manager_.SetStreamsAllowed(true);
}

void BaseConnection::ScheduleProbeRetry() {
    event_loop_->RemoveTimer(path_probe_task_);
    if (!path_probe_inflight_) return;

    if (probe_retry_count_ >= 5) {
        // Give up probing after max retries; revert to old path
        common::LOG_WARN("Path validation failed after %d attempts, reverting to old path. candidate: %s:%d",
            probe_retry_count_, candidate_peer_addr_.GetIp().c_str(), candidate_peer_addr_.GetPort());

        // Clean up probe state
        path_probe_inflight_ = false;
        candidate_peer_addr_ = common::Address();
        memset(pending_path_challenge_data_, 0, sizeof(pending_path_challenge_data_));

        // Critical: restore stream sending capability
        ExitAntiAmplification();

        // Start probing next address in queue if any
        StartNextPathProbe();
        return;
    }

    probe_retry_count_++;
    probe_retry_delay_ms_ = std::min<uint32_t>(probe_retry_delay_ms_ * 2, 2000);
    path_probe_task_.SetTimeoutCallback([this]() {
        if (!path_probe_inflight_) return;
        auto challenge = std::make_shared<PathChallengeFrame>();
        challenge->MakeData();
        common::LOG_DEBUG("Retrying path validation (attempt %d/%d) to %s:%d", probe_retry_count_ + 1, 5,
            candidate_peer_addr_.GetIp().c_str(), candidate_peer_addr_.GetPort());
        memcpy(pending_path_challenge_data_, challenge->GetData(), 8);
        ToSendFrame(challenge);
        ScheduleProbeRetry();
    });
    event_loop_->AddTimer(path_probe_task_, probe_retry_delay_ms_, 0);
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
    if (addr == peer_addr_) {
        return;
    }

    common::LOG_INFO("Observed new peer address: %s:%d (current: %s:%d)", addr.GetIp().c_str(), addr.GetPort(),
        peer_addr_.GetIp().c_str(), peer_addr_.GetPort());

    // Respect disable_active_migration: ignore proactive migration but allow NAT rebinding
    // Heuristic: if we have received any packet from the new address (workers call
    // OnCandidatePathDatagramReceived before this frame processing), it's likely NAT rebinding.
    if (transport_param_.GetDisableActiveMigration()) {
        // Only consider as NAT rebinding if we see repeated observations; otherwise ignore
        if (!(addr == candidate_peer_addr_)) {
            // First observation: store candidate but do not start probe yet
            common::LOG_DEBUG("First observation of new address (migration disabled), waiting for confirmation");
            candidate_peer_addr_ = addr;
            return;
        }
        // Second consecutive observation of same new address: treat as rebinding and probe
        common::LOG_INFO("Second observation confirmed, treating as NAT rebinding");
    }

    // Check if this address is already in the queue or currently being probed
    if (path_probe_inflight_ && addr == candidate_peer_addr_) {
        common::LOG_DEBUG("Address %s:%d is already being probed, ignoring", addr.GetIp().c_str(), addr.GetPort());
        return;
    }

    for (const auto& pending : pending_candidate_addrs_) {
        if (addr == pending) {
            common::LOG_DEBUG("Address %s:%d already in probe queue, ignoring", addr.GetIp().c_str(), addr.GetPort());
            return;
        }
    }

    // If probe is in progress, add to queue; otherwise start immediately
    if (path_probe_inflight_) {
        pending_candidate_addrs_.push_back(addr);
        common::LOG_INFO("Added %s:%d to probe queue (queue size: %zu)", addr.GetIp().c_str(), addr.GetPort(),
            pending_candidate_addrs_.size());
    } else {
        candidate_peer_addr_ = addr;
        StartPathValidationProbe();

        // If probe didn't start (e.g., Application keys not ready), queue the address for later
        if (!path_probe_inflight_) {
            pending_candidate_addrs_.push_back(addr);
            common::LOG_INFO("Path probe deferred, added %s:%d to queue (queue size: %zu)", addr.GetIp().c_str(),
                addr.GetPort(), pending_candidate_addrs_.size());
        } else {
            common::LOG_INFO("Started path validation probe to %s:%d", addr.GetIp().c_str(), addr.GetPort());
        }
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
        auto cid = local_conn_id_manager_->GetCurrentID();
        ((LongHeader*)header)->SetSourceConnectionId(cid.GetID(), cid.GetLength());
        ((LongHeader*)header)->SetVersion(kQuicVersions[0]);
    }
    auto remote_cid = remote_conn_id_manager_->GetCurrentID();
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
    if (immediate_send_cb_) {
        immediate_send_cb_(send_buffer, peer_addr_);
        common::LOG_DEBUG("SendImmediateAckAtLevel: ACK packet sent successfully");
    } else {
        common::LOG_WARN("SendImmediateAckAtLevel: no immediate_send_cb_ set, packet not sent!");
        return false;
    }

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
    common::LOG_INFO("BaseConnection::ImmediateClose called. error=%llu, reason=%s", error, reason.c_str());

    // Cancel graceful close if it's pending
    if (graceful_closing_pending_) {
        common::LOG_DEBUG("Canceling graceful close due to immediate close");
        graceful_closing_pending_ = false;
        event_loop_->RemoveTimer(graceful_close_timer_);
    }

    // Error close: enter Closing state
    // Store error info for retransmission
    closing_error_code_ = error;
    closing_trigger_frame_ = tigger_frame;
    closing_reason_ = reason;
    last_connection_close_retransmit_time_ = 0;  // Reset retransmit timer

    // Cancel all streams
    for (auto& stream : streams_map_) {
        stream.second->Reset(error);
    }

    state_machine_.OnClose();
}

void BaseConnection::InnerStreamClose(uint64_t stream_id) {
    // remove stream
    auto stream = streams_map_.find(stream_id);
    if (stream != streams_map_.end()) {
        streams_map_.erase(stream);

        // Metrics: Stream closed
        common::Metrics::GaugeDec(common::MetricsStd::QuicStreamsActive);
        common::Metrics::CounterInc(common::MetricsStd::QuicStreamsClosed);
    }
}

void BaseConnection::OnStreamDataAcked(uint64_t stream_id, uint64_t max_offset, bool has_fin) {
    common::LOG_DEBUG("OnStreamDataAcked: stream_id=%llu, max_offset=%llu, has_fin=%d", stream_id, max_offset, has_fin);

    auto it = streams_map_.find(stream_id);
    if (it == streams_map_.end()) {
        common::LOG_DEBUG("OnStreamDataAcked: stream %llu not found (already closed)", stream_id);
        return;
    }

    // Notify stream about data ACK
    // Cast to SendStream to call OnDataAcked (will be implemented next)
    auto send_stream = std::dynamic_pointer_cast<SendStream>(it->second);
    if (send_stream) {
        send_stream->OnDataAcked(max_offset, has_fin);
    }
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

std::shared_ptr<IStream> BaseConnection::MakeStream(uint32_t init_size, uint64_t stream_id, StreamDirection sd) {
    std::shared_ptr<IStream> new_stream;
    if (sd == StreamDirection::kBidi) {
        new_stream = std::make_shared<BidirectionStream>(event_loop_, init_size, stream_id,
            std::bind(&BaseConnection::ActiveSendStream, this, std::placeholders::_1),
            std::bind(&BaseConnection::InnerStreamClose, this, std::placeholders::_1),
            std::bind(&BaseConnection::InnerConnectionClose, this, std::placeholders::_1, std::placeholders::_2,
                std::placeholders::_3));

    } else if (sd == StreamDirection::kSend) {
        new_stream = std::make_shared<SendStream>(event_loop_, init_size, stream_id,
            std::bind(&BaseConnection::ActiveSendStream, this, std::placeholders::_1),
            std::bind(&BaseConnection::InnerStreamClose, this, std::placeholders::_1),
            std::bind(&BaseConnection::InnerConnectionClose, this, std::placeholders::_1, std::placeholders::_2,
                std::placeholders::_3));

    } else {
        new_stream = std::make_shared<RecvStream>(event_loop_, init_size, stream_id,
            std::bind(&BaseConnection::ActiveSendStream, this, std::placeholders::_1),
            std::bind(&BaseConnection::InnerStreamClose, this, std::placeholders::_1),
            std::bind(&BaseConnection::InnerConnectionClose, this, std::placeholders::_1, std::placeholders::_2,
                std::placeholders::_3));
    }
    // Metrics: Stream created
    common::Metrics::GaugeInc(common::MetricsStd::QuicStreamsActive);
    common::Metrics::CounterInc(common::MetricsStd::QuicStreamsCreated);

    return new_stream;
}

void BaseConnection::CheckAndReplenishLocalCIDPool() {
    if (!local_conn_id_manager_) {
        return;
    }

    size_t current_count = local_conn_id_manager_->GetAvailableIDCount();

    // Ensure we have enough *spare* CIDs beyond the one currently in use
    // For connection migration, the peer needs additional CIDs to switch to
    // current_count includes the CID currently in use, so we need total >= kMinLocalCIDPoolSize + 1
    // to ensure kMinLocalCIDPoolSize spare CIDs
    if (current_count >= kMinLocalCIDPoolSize + 1) {
        return;
    }

    // Calculate how many CIDs to generate (up to max pool size)
    size_t to_generate = std::min<size_t>(kMaxLocalCIDPoolSize - current_count, kMaxLocalCIDPoolSize);

    common::LOG_DEBUG("replenishing local CID pool: current=%zu, generating=%zu", current_count, to_generate);

    for (size_t i = 0; i < to_generate; ++i) {
        // Generate new connection ID
        ConnectionID new_cid = local_conn_id_manager_->Generator();

        // Create and send NEW_CONNECTION_ID frame
        auto frame = std::make_shared<NewConnectionIDFrame>();
        frame->SetSequenceNumber(new_cid.GetSequenceNumber());
        frame->SetRetirePriorTo(0);  // Don't force retirement of older IDs
        frame->SetConnectionID(const_cast<uint8_t*>(new_cid.GetID()), new_cid.GetLength());

        // Generate stateless reset token (using random data for now)
        // In production, this should be derived from a secret
        uint8_t reset_token[16];
        for (int j = 0; j < 16; ++j) {
            reset_token[j] = static_cast<uint8_t>(rand() % 256);
        }
        frame->SetStatelessResetToken(reset_token);

        ToSendFrame(frame);

        common::LOG_DEBUG(
            "Generated NEW_CONNECTION_ID: seq=%llu, len=%d", new_cid.GetSequenceNumber(), new_cid.GetLength());
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
    frame->SetErrorCode(closing_error_code_);
    frame->SetErrFrameType(closing_trigger_frame_);
    frame->SetReason(closing_reason_);

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
    last_connection_close_retransmit_time_ = common::UTCTimeMsec();

    common::TimerTask task(std::bind(&BaseConnection::OnClosingTimeout, this));
    event_loop_->AddTimer(task, GetCloseWaitTime() * 3, 0);
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
    if (connection_close_cb_ && !connection_close_cb_invoked_) {
        connection_close_cb_invoked_ = true;
        common::LOG_INFO("OnStateToDraining: notifying application layer of connection close");
        connection_close_cb_(shared_from_this(), closing_error_code_, closing_reason_);
    }

    common::TimerTask task(std::bind(&BaseConnection::OnClosingTimeout, this));
    event_loop_->AddTimer(task, GetCloseWaitTime() * 3, 0);
}

void BaseConnection::OnStateToClosed() {
    // Log connection_closed event
    if (qlog_trace_) {
        common::ConnectionClosedData data;
        data.error_code = closing_error_code_;
        data.reason = closing_reason_;

        // Determine trigger based on error code
        if (closing_error_code_ == 0) {
            data.trigger = "clean";
        } else if (closing_trigger_frame_ != 0) {
            data.trigger = "error";
        } else {
            data.trigger = "application";
        }

        QLOG_CONNECTION_CLOSED(qlog_trace_, data);
        qlog_trace_->Flush();  // 确保事件写入
    }

    event_loop_->RemoveTimer(idle_timeout_task_);

    // Only invoke callback if it hasn't been called yet
    // (may have been called earlier in OnStateToDraining)
    if (connection_close_cb_ && !connection_close_cb_invoked_) {
        connection_close_cb_invoked_ = true;
        connection_close_cb_(shared_from_this(), QuicErrorCode::kNoError, "normal close.");
    }
}

}  // namespace quic
}  // namespace quicx