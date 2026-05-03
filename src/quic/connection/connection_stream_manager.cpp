#include "common/log/log.h"
#include "common/metrics/metrics.h"
#include "common/metrics/metrics_std.h"
#include "common/qlog/qlog.h"

#include "common/log/log_context.h"
#include "quic/connection/connection_stream_manager.h"
#include "quic/connection/controler/send_flow_controller.h"
#include "quic/connection/controler/send_manager.h"
#include "quic/connection/if_connection_event_sink.h"
#include "quic/connection/transport_param.h"
#include "quic/crypto/tls/type.h"
#include "quic/frame/if_frame.h"
#include "quic/stream/bidirection_stream.h"
#include "quic/stream/crypto_stream.h"
#include "quic/stream/if_frame_visitor.h"
#include "quic/stream/if_stream.h"
#include "quic/stream/recv_stream.h"
#include "quic/stream/send_stream.h"

namespace quicx {
namespace quic {

namespace {
const char* StreamStateToString(StreamState state) {
    switch (state) {
        case StreamState::kReady:     return "ready";
        case StreamState::kSend:      return "send";
        case StreamState::kDataSent:  return "data_sent";
        case StreamState::kResetSent: return "reset_sent";
        case StreamState::kRecv:      return "recv";
        case StreamState::kSizeKnown: return "size_known";
        case StreamState::kDataRead:  return "data_read";
        case StreamState::kResetRead: return "reset_read";
        case StreamState::kDataRecvd: return "data_recvd";
        case StreamState::kResetRecvd:return "reset_recvd";
        default:                      return "unknown";
    }
}
}  // anonymous namespace

StreamManager::StreamManager(IConnectionEventSink& event_sink, std::shared_ptr<::quicx::common::IEventLoop> event_loop,
    TransportParam& transport_param, SendManager& send_manager, StreamStateCallback stream_state_cb,
    SendFlowController* send_flow_controller):
    event_sink_(event_sink),
    event_loop_(event_loop),
    send_flow_controller_(send_flow_controller),
    transport_param_(transport_param),
    send_manager_(send_manager),
    stream_state_cb_(stream_state_cb) {}

// ==================== Stream Creation ====================

std::shared_ptr<IStream> StreamManager::MakeStreamWithFlowControl(StreamDirection type) {
    // Check streams limit using NEW flow controller
    uint64_t stream_id;
    std::shared_ptr<IFrame> frame;
    bool can_make_stream = false;

    if (!send_flow_controller_) {
        common::LOG_ERROR("StreamManager::MakeStreamWithFlowControl: send_flow_controller is null");
        return nullptr;
    }

    // Use new flow controller exclusively - clean migration
    if (type == StreamDirection::kSend) {
        can_make_stream = send_flow_controller_->CanCreateUniStream(stream_id, frame);
    } else {
        can_make_stream = send_flow_controller_->CanCreateBidiStream(stream_id, frame);
    }

    // Send STREAMS_BLOCKED frame if generated (using event interface)
    if (frame) {
        event_sink_.OnFrameReady(frame);
    }

    if (!can_make_stream) {
        common::LOG_DEBUG("StreamManager::MakeStreamWithFlowControl: stream limit reached");
        return nullptr;
    }

    // Determine initial flow control limits
    uint32_t send_size;
    uint32_t recv_size = 0;  // 0 means same as send_size (for uni streams)
    if (type == StreamDirection::kSend) {
        send_size = static_cast<uint32_t>(transport_param_.GetInitialMaxStreamDataUni());
        common::LOG_DEBUG("StreamManager::MakeStreamWithFlowControl: GetInitialMaxStreamDataUni=%u", send_size);
    } else {
        // Bidi stream: separate send and recv limits
        // Send limit = peer's bidi_remote (how much peer allows us to send on our-initiated bidi streams)
        send_size = static_cast<uint32_t>(transport_param_.GetPeerInitialMaxStreamDataBidiRemote());
        // Recv limit = our bidi_local (how much we allow peer to send on our-initiated bidi streams)
        recv_size = static_cast<uint32_t>(transport_param_.GetInitialMaxStreamDataBidiLocal());
        common::LOG_DEBUG("StreamManager::MakeStreamWithFlowControl: bidi send_size=%u (peer bidi_remote), recv_size=%u (local bidi_local)",
            send_size, recv_size);
    }

    // Create stream
    auto new_stream = MakeStream(send_size, stream_id, type, recv_size);

    // Metrics: Stream created
    common::Metrics::GaugeInc(common::MetricsStd::QuicStreamsActive);
    common::Metrics::CounterInc(common::MetricsStd::QuicStreamsCreated);

    return new_stream;
}

bool StreamManager::MakeStreamAsync(StreamDirection type, stream_creation_callback callback) {
    // Try to create stream immediately
    auto stream = MakeStreamWithFlowControl(type);
    if (stream) {
        // Success: invoke callback immediately
        if (callback) {
            event_loop_->PostTask([callback, stream]() { callback(stream); });
        }
        return true;
    }

    // Stream limit reached: queue the request
    std::lock_guard<std::mutex> lock(pending_streams_mutex_);
    pending_stream_requests_.push({type, callback});
    common::LOG_DEBUG("StreamManager: queued stream request (type=%d), queue size=%zu", static_cast<int>(type),
        pending_stream_requests_.size());
    return false;
}

void StreamManager::RetryPendingStreamRequests() {
    std::lock_guard<std::mutex> lock(pending_streams_mutex_);

    while (!pending_stream_requests_.empty()) {
        auto& req = pending_stream_requests_.front();

        // Try to create the stream
        auto stream = MakeStreamWithFlowControl(req.type);
        if (!stream) {
            // Still can't create, stop retrying
            common::LOG_DEBUG("StreamManager: retry failed, still at stream limit");
            break;
        }

        // Success: invoke the callback
        common::LOG_INFO("StreamManager: successfully created queued stream (type=%d), remaining=%zu",
            static_cast<int>(req.type), pending_stream_requests_.size() - 1);

        if (req.callback) {
            auto callback = req.callback;
            event_loop_->PostTask([callback, stream]() { callback(stream); });
        }

        pending_stream_requests_.pop();
    }
}

std::shared_ptr<IStream> StreamManager::MakeStream(uint32_t send_size, uint64_t stream_id, StreamDirection type, uint32_t recv_size) {
    // If recv_size is 0, use send_size for both (backward compatibility for uni-directional streams)
    if (recv_size == 0) {
        recv_size = send_size;
    }

    // Create lambda wrappers to bridge stream callbacks to event interface
    // This avoids need to refactor all stream classes immediately
    auto active_send_cb = [this](std::shared_ptr<IStream> stream) { event_sink_.OnStreamDataReady(stream); };
    auto stream_close_cb = [this](uint64_t stream_id) { event_sink_.OnStreamClosed(stream_id); };
    auto connection_close_cb = [this](uint64_t error, uint16_t frame_type, const std::string& reason) {
        event_sink_.OnConnectionClose(error, frame_type, reason);
    };

    // Create stream object based on type
    std::shared_ptr<IStream> stream;
    if (type == StreamDirection::kBidi) {
        stream = std::make_shared<BidirectionStream>(
            event_loop_, send_size, recv_size, stream_id, active_send_cb, stream_close_cb, connection_close_cb);
    } else if (type == StreamDirection::kSend) {
        stream = std::make_shared<SendStream>(
            event_loop_, send_size, stream_id, active_send_cb, stream_close_cb, connection_close_cb);
    } else {
        stream = std::make_shared<RecvStream>(
            event_loop_, recv_size, stream_id, active_send_cb, stream_close_cb, connection_close_cb);
    }

    common::LOG_DEBUG("StreamManager::MakeStream: created stream %llu, type %d, send_size %u, recv_size %u", stream_id,
        static_cast<int>(type), send_size, recv_size);

    if (stream) {
        // Set qlog state change callback on stream state machines
        if (qlog_trace_) {
            auto trace = qlog_trace_;
            auto state_change_cb = [trace](uint64_t sid, StreamState old_s, StreamState new_s) {
                common::StreamStateUpdatedData data;
                data.stream_id = sid;
                data.old_state = StreamStateToString(old_s);
                data.new_state = StreamStateToString(new_s);
                QLOG_STREAM_STATE_UPDATED(trace, data);
            };
            // For send streams and bidirectional streams
            auto send = std::dynamic_pointer_cast<SendStream>(stream);
            if (send && send->GetSendStateMachine()) {
                send->GetSendStateMachine()->SetStateChangeCB(state_change_cb, stream_id);
            }
            // For recv streams and bidirectional streams
            auto recv = std::dynamic_pointer_cast<RecvStream>(stream);
            if (recv && recv->GetRecvStateMachine()) {
                recv->GetRecvStateMachine()->SetStateChangeCB(state_change_cb, stream_id);
            }
        }
        streams_map_[stream_id] = stream;
        common::LOG_DEBUG("StreamManager: created stream %llu (type=%d)", stream_id, static_cast<int>(type));
    }
    return stream;
}

// ==================== Stream Closure ====================

void StreamManager::CloseStream(uint64_t stream_id) {
    auto iter = streams_map_.find(stream_id);
    if (iter == streams_map_.end()) {
        return;
    }

    streams_map_.erase(iter);

    common::LOG_DEBUG("StreamManager: closed stream %llu", stream_id);

    // Note: Stream state callback and send manager cleanup are handled by
    // BaseConnection's InnerStreamClose which calls this method.
}

// ==================== Stream Lookup ====================

std::shared_ptr<IStream> StreamManager::FindStream(uint64_t stream_id) {
    auto iter = streams_map_.find(stream_id);
    if (iter != streams_map_.end()) {
        return iter->second;
    }
    return nullptr;
}

// ==================== Remote Stream Creation ====================

std::shared_ptr<IStream> StreamManager::CreateRemoteStream(
    uint32_t send_size, uint64_t stream_id, StreamDirection direction, uint32_t recv_size) {
    // Check if stream already exists
    if (streams_map_.find(stream_id) != streams_map_.end()) {
        common::LOG_WARN("StreamManager::CreateRemoteStream: stream %llu already exists", stream_id);
        return streams_map_[stream_id];
    }

    // Create the stream (flow control checks are done by caller)
    auto stream = MakeStream(send_size, stream_id, direction, recv_size);
    if (stream) {
        streams_map_[stream_id] = stream;
        common::LOG_DEBUG(
            "StreamManager: created remote stream %llu (type=%d)", stream_id, static_cast<int>(direction));
    }

    return stream;
}

// ==================== Stream ACK Notification ====================

void StreamManager::OnStreamDataAcked(uint64_t stream_id, uint64_t max_offset, bool has_fin) {
    common::LogTagGuard guard("|strm:" + std::to_string(stream_id));
    auto stream = FindStream(stream_id);
    if (!stream) {
        common::LOG_DEBUG("StreamManager: stream %llu not found in OnStreamDataAcked", stream_id);
        return;
    }

    // Only SendStream has OnDataAcked method
    auto send_stream = std::dynamic_pointer_cast<SendStream>(stream);
    if (!send_stream) {
        common::LOG_DEBUG("StreamManager: stream %llu is not a SendStream", stream_id);
        return;
    }

    // Notify stream about ACK - the stream will handle closing itself if needed
    send_stream->OnDataAcked(max_offset, has_fin);
}

// ==================== Stream Reset ====================

void StreamManager::ResetAllStreams(uint64_t error) {
    common::LOG_DEBUG("StreamManager: resetting all %zu streams with error %llu", streams_map_.size(), error);

    // Iterate over all streams and reset them
    for (auto& stream_pair : streams_map_) {
        stream_pair.second->Reset(error);
    }
}

std::vector<uint64_t> StreamManager::GetAllStreamIDs() const {
    std::vector<uint64_t> stream_ids;
    stream_ids.reserve(streams_map_.size());

    for (const auto& stream_pair : streams_map_) {
        stream_ids.push_back(stream_pair.first);
    }

    return stream_ids;
}

// ==================== Stream Scheduling (Week 4 Refactoring) ====================

void StreamManager::MarkStreamActive(std::shared_ptr<IStream> stream) {
    if (!stream) {
        common::LOG_ERROR("StreamManager::MarkStreamActive: null stream");
        return;
    }

    uint64_t stream_id = stream->GetStreamID();
    common::LogTagGuard guard("|strm:" + std::to_string(stream_id));
    common::LOG_DEBUG("StreamManager: marking stream %llu as active", stream_id);

    // Add to write buffer (safe during BuildStreamFrames processing)
    active_streams_.Add(stream);

    // NOTE: Do NOT call event_sink_.OnStreamDataReady(stream) here!
    // This would cause infinite recursion:
    // ActiveSendStream -> MarkStreamActive -> OnStreamDataReady -> ActiveSendStream -> ...
    // The caller (ActiveSendStream) already handles triggering the send via ActiveSend()
}

bool StreamManager::BuildStreamFrames(IFrameVisitor* visitor, uint8_t encrypto_level) {
    if (!visitor) {
        common::LOG_ERROR("StreamManager::BuildStreamFrames: null visitor");
        return false;
    }

    // Swap buffers: move unfinished streams from read to write buffer
    active_streams_.Swap();
    auto& streams = active_streams_.GetReadBuffer();

    bool has_more_data = false;
    bool all_flow_control_blocked = true;  // Track if ALL streams are flow control blocked

    // Iterate through active streams
    for (auto iter = streams.begin(); iter != streams.end();) {
        auto stream = *iter;
        if (!stream) {
            iter = streams.erase(iter);
            continue;
        }

        uint64_t sid = stream->GetStreamID();
        common::LogTagGuard guard("|strm:" + std::to_string(sid));

        // Encryption level filtering (RFC 9000 §12.4 / §12.5):
        //  - CRYPTO frames: allowed at Initial/Handshake/Application (handled by CryptoStream per-level buffer)
        //  - STREAM frames: only allowed at 0-RTT (kEarlyData) or 1-RTT (kApplication)
        // The CryptoStream uses stream_id == 0, BUT a client-initiated bidi stream can ALSO have id == 0
        // (RFC 9000 §2.1: client-initiated bidi stream ids = 0, 4, 8, ...), so we MUST differentiate
        // by actual object type, not by stream_id.
        bool is_crypto_stream = (std::dynamic_pointer_cast<CryptoStream>(stream) != nullptr);
        common::LOG_DEBUG("StreamManager loop: stream %llu, level %u, is_crypto=%d", sid, encrypto_level,
            is_crypto_stream ? 1 : 0);
        if (!is_crypto_stream && !(encrypto_level == kEarlyData || encrypto_level == kApplication)) {
            // STREAM frames are not allowed at Initial/Handshake encryption levels.
            // Keep the stream in the active list so it can be sent at a later level.
            common::LOG_DEBUG("StreamManager: stream %llu deferred (encryption level %u)", sid, encrypto_level);
            has_more_data = true;
            all_flow_control_blocked = false;  // Deferred is not flow control blocked
            ++iter;    // Move to next stream
            continue;  // Skip this stream and continue with others
        }

        // Try to send stream data
        common::LOG_DEBUG("StreamManager: building frames for stream %llu", sid);
        auto ret = stream->TrySendData(visitor, (EncryptionLevel)encrypto_level);

        if (ret == IStream::TrySendResult::kSuccess) {
            // Stream data sent successfully, remove from active list
            common::LOG_DEBUG("StreamManager: stream %llu send complete", sid);
            iter = streams.erase(iter);
            all_flow_control_blocked = false;  // Successfully sent data

        } else if (ret == IStream::TrySendResult::kFailed) {
            // Stream send failed (permanent error), remove from active list
            common::LOG_WARN("StreamManager: stream %llu send failed, removing", sid);
            iter = streams.erase(iter);
            all_flow_control_blocked = false;  // Failed is not flow control blocked

        } else if (ret == IStream::TrySendResult::kFlowControlBlocked) {
            // Flow control blocked, keep in active list waiting for MAX_STREAM_DATA
            // Note: STREAM_DATA_BLOCKED frame was already sent by TrySendData
            common::LOG_DEBUG("StreamManager: stream %llu flow control blocked, keeping in active list", sid);
            has_more_data = true;
            // Don't set all_flow_control_blocked = false, this stream IS flow control blocked
            ++iter;  // Move to next stream, don't remove

        } else if (ret == IStream::TrySendResult::kBreak) {
            // Packet full, but stream has more data
            // Keep in active list for next packet
            common::LOG_INFO("StreamManager: packet full, stream %llu will retry", sid);
            has_more_data = true;
            all_flow_control_blocked = false;  // Break means we can send more
            break;
        }
    }

    // If ALL streams are flow control blocked, return false to stop the send loop
    // This prevents infinite sending of STREAM_DATA_BLOCKED frames
    // The streams remain in active list and will be retried when MAX_STREAM_DATA arrives
    if (has_more_data && all_flow_control_blocked) {
        common::LOG_DEBUG("StreamManager: all streams flow control blocked, stopping send loop");
        return false;  // Stop send loop, wait for MAX_STREAM_DATA
    }

    return has_more_data;
}

void StreamManager::ClearActiveStreams() {
    common::LOG_DEBUG("StreamManager: clearing all active streams");
    active_streams_.Clear();
}

bool StreamManager::HasActiveStreamsForLevel(uint8_t level) const {
    if (active_streams_.IsEmpty()) {
        return false;
    }

    // For Application and EarlyData levels, any active stream qualifies
    if (level == kApplication || level == kEarlyData) {
        return true;
    }

    // For Initial/Handshake levels, only the CryptoStream can produce frames
    // (CRYPTO frames). Application streams cannot send STREAM frames at these
    // levels per RFC 9000 §12.4 / §12.5.
    // NOTE: CryptoStream uses stream_id == 0, but a client-initiated bidi
    // stream can ALSO have id == 0 (RFC 9000 §2.1: client bidi ids = 0,4,8,...).
    // So we MUST differentiate by actual object type via dynamic_pointer_cast,
    // NOT by stream_id value — otherwise a legit bidi stream 0 would cause us
    // to wrongly schedule STREAM frames at Initial/Handshake level.
    // Check both buffers since crypto stream may be in either one depending
    // on timing relative to Swap().
    const auto& read_buf = active_streams_.GetReadBuffer();
    for (const auto& stream : read_buf) {
        if (stream && std::dynamic_pointer_cast<CryptoStream>(stream) != nullptr) {
            return true;
        }
    }
    const auto& write_buf = active_streams_.GetWriteBuffer();
    for (const auto& stream : write_buf) {
        if (stream && std::dynamic_pointer_cast<CryptoStream>(stream) != nullptr) {
            return true;
        }
    }

    return false;
}

}  // namespace quic
}  // namespace quicx
