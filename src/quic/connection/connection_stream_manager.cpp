#include "common/log/log.h"
#include "common/metrics/metrics.h"
#include "common/metrics/metrics_std.h"

#include "quic/connection/connection_stream_manager.h"
#include "quic/connection/controler/send_flow_controller.h"
#include "quic/connection/controler/send_manager.h"
#include "quic/connection/if_connection_event_sink.h"
#include "quic/connection/transport_param.h"
#include "quic/crypto/tls/type.h"
#include "quic/frame/if_frame.h"
#include "quic/stream/bidirection_stream.h"
#include "quic/stream/if_frame_visitor.h"
#include "quic/stream/if_stream.h"
#include "quic/stream/recv_stream.h"
#include "quic/stream/send_stream.h"

namespace quicx {
namespace quic {

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

    // Determine initial size
    uint32_t init_size;
    if (type == StreamDirection::kSend) {
        init_size = transport_param_.GetInitialMaxStreamDataUni();
    } else {
        init_size = transport_param_.GetInitialMaxStreamDataBidiLocal();
    }

    // Create stream
    auto new_stream = MakeStream(init_size, stream_id, type);

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

std::shared_ptr<IStream> StreamManager::MakeStream(uint32_t init_size, uint64_t stream_id, StreamDirection type) {
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
        stream = std::make_shared<BidirectionStream>(event_loop_, init_size, stream_id, active_send_cb,
            stream_close_cb, connection_close_cb);
    } else if (type == StreamDirection::kSend) {
        stream = std::make_shared<SendStream>(event_loop_, init_size, stream_id, active_send_cb, stream_close_cb,
            connection_close_cb);
    } else {
        stream = std::make_shared<RecvStream>(event_loop_, init_size, stream_id, active_send_cb, stream_close_cb,
            connection_close_cb);
    }

    if (stream) {
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
    uint32_t init_size, uint64_t stream_id, StreamDirection direction) {
    // Check if stream already exists
    if (streams_map_.find(stream_id) != streams_map_.end()) {
        common::LOG_WARN("StreamManager::CreateRemoteStream: stream %llu already exists", stream_id);
        return streams_map_[stream_id];
    }

    // Create the stream (flow control checks are done by caller)
    auto stream = MakeStream(init_size, stream_id, direction);
    if (stream) {
        streams_map_[stream_id] = stream;
        common::LOG_DEBUG(
            "StreamManager: created remote stream %llu (type=%d)", stream_id, static_cast<int>(direction));
    }

    return stream;
}

// ==================== Stream ACK Notification ====================

void StreamManager::OnStreamDataAcked(uint64_t stream_id, uint64_t max_offset, bool has_fin) {
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

    // Iterate through active streams
    for (auto iter = streams.begin(); iter != streams.end();) {
        auto stream = *iter;
        if (!stream) {
            iter = streams.erase(iter);
            continue;
        }

        uint64_t sid = stream->GetStreamID();

        // Encryption level filtering (from SendManager::MakePacket logic)
        // Crypto stream (id == 0) can be sent at any level
        // Application streams (id != 0) can only be sent at 0-RTT or 1-RTT
        if (sid != 0 && !(encrypto_level == kEarlyData || encrypto_level == kApplication)) {
            // Current encryption level doesn't allow this stream
            // Keep it in the active list for later encryption levels
            common::LOG_DEBUG("StreamManager: stream %llu deferred (encryption level %u)", sid, encrypto_level);
            has_more_data = true;
            ++iter;  // Move to next stream
            continue;  // Skip this stream and continue with others
        }

        // Try to send stream data
        common::LOG_DEBUG("StreamManager: building frames for stream %llu", sid);
        auto ret = stream->TrySendData(visitor);

        if (ret == IStream::TrySendResult::kSuccess) {
            // Stream data sent successfully, remove from active list
            common::LOG_DEBUG("StreamManager: stream %llu send complete", sid);
            iter = streams.erase(iter);

        } else if (ret == IStream::TrySendResult::kFailed) {
            // Stream send failed (flow control or error), remove to prevent infinite retry
            common::LOG_WARN("StreamManager: stream %llu send failed, removing", sid);
            iter = streams.erase(iter);

        } else if (ret == IStream::TrySendResult::kBreak) {
            // Packet full, but stream has more data
            // Keep in active list for next packet
            common::LOG_INFO("StreamManager: packet full, stream %llu will retry", sid);
            has_more_data = true;
            break;
        }
    }

    return has_more_data;
}

void StreamManager::ClearActiveStreams() {
    common::LOG_DEBUG("StreamManager: clearing all active streams");
    active_streams_.Clear();
}

}  // namespace quic
}  // namespace quicx
