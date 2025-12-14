#include "common/log/log.h"
#include "common/metrics/metrics.h"
#include "common/metrics/metrics_std.h"

#include "quic/connection/connection_stream_manager.h"
#include "quic/stream/bidirection_stream.h"

namespace quicx {
namespace quic {

StreamManager::StreamManager(std::shared_ptr<::quicx::common::IEventLoop> event_loop,
    ConnectionFlowControl& flow_control, TransportParam& transport_param, StreamManager& send_manager,
    StreamStateCallback stream_state_cb, ToSendFrameCallback to_send_frame_cb,
    ActiveSendStreamCallback active_send_stream_cb, InnerStreamCloseCallback inner_stream_close_cb,
    InnerConnectionCloseCallback inner_connection_close_cb):
    event_loop_(event_loop),
    flow_control_(flow_control),
    transport_param_(transport_param),
    send_manager_(send_manager),
    stream_state_cb_(stream_state_cb),
    to_send_frame_cb_(to_send_frame_cb),
    active_send_stream_cb_(active_send_stream_cb),
    inner_stream_close_cb_(inner_stream_close_cb),
    inner_connection_close_cb_(inner_connection_close_cb) {}

// ==================== Stream Creation ====================

std::shared_ptr<IStream> StreamManager::MakeStreamWithFlowControl(StreamDirection type) {
    // Check streams limit
    uint64_t stream_id;
    std::shared_ptr<IFrame> frame;
    bool can_make_stream = false;
    if (type == StreamDirection::kSend) {
        can_make_stream = flow_control_.CheckPeerControlUnidirectionStreamLimit(stream_id, frame);
    } else {
        can_make_stream = flow_control_.CheckPeerControlBidirectionStreamLimit(stream_id, frame);
    }
    if (frame && to_send_frame_cb_) {
        to_send_frame_cb_(frame);
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
    // Create stream object directly based on type
    std::shared_ptr<IStream> stream;
    if (type == StreamDirection::kBidi) {
        stream = std::make_shared<BidirectionStream>(event_loop_, init_size, stream_id, active_send_stream_cb_,
            inner_stream_close_cb_, inner_connection_close_cb_);
    } else if (type == StreamDirection::kSend) {
        stream = std::make_shared<SendStream>(event_loop_, init_size, stream_id, active_send_stream_cb_,
            inner_stream_close_cb_, inner_connection_close_cb_);
    } else {
        stream = std::make_shared<RecvStream>(event_loop_, init_size, stream_id, active_send_stream_cb_,
            inner_stream_close_cb_, inner_connection_close_cb_);
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

void StreamManager::ToSendFrame(std::shared_ptr<IFrame> frame) {
    wait_frame_list_.emplace_front(frame);
}

void StreamManager::ActiveStream(std::shared_ptr<IStream> stream) {
    common::LOG_DEBUG("active stream. stream id:%d", stream->GetStreamID());
    // Always add to the write set, which is safe even during MakePacket processing
    auto& write_set = GetWriteActiveSendStreamSet();
    write_set.add(stream);
    common::LOG_DEBUG("active stream added to write set. stream id:%d, is_current:%d", stream->GetStreamID(),
        active_send_stream_set_1_is_current_ ? 1 : 2);
}

bool StreamManager::GetSendData(
    std::shared_ptr<common::IBuffer> buffer, uint8_t encrypto_level, std::shared_ptr<ICryptographer> cryptographer) {
    return send_manager_.GetSendData(buffer, encrypto_level, cryptographer);
}

// Dual-buffer mechanism implementation (similar to Worker::GetReadActiveSendConnectionSet)
ActiveStreamSet& StreamManager::GetReadActiveSendStreamSet() {
    return active_send_stream_set_1_is_current_ ? active_send_stream_set_1_ : active_send_stream_set_2_;
}

ActiveStreamSet& StreamManager::GetWriteActiveSendStreamSet() {
    // Write set is always the opposite of read set
    return active_send_stream_set_1_is_current_ ? active_send_stream_set_2_ : active_send_stream_set_1_;
}

void StreamManager::SwitchActiveSendStreamSet() {
    // This method is called at the beginning of MakePacket to switch the active sets.
    // It merges any remaining streams from the read set into the write set,
    // then switches the read/write sets so that:
    // - The old write set becomes the new read set (to be processed)
    // - The old read set becomes the new write set (for new additions)
    //
    // This ensures that:
    // 1. Streams added during callbacks (in write set) are safe from being removed
    // 2. Any streams that weren't fully processed in the previous read set are preserved
    // 3. The next MakePacket will process streams from the new read set

    auto& read_set = GetReadActiveSendStreamSet();
    auto& write_set = GetWriteActiveSendStreamSet();

    // Move any remaining streams from read set to write set
    while (!read_set.queue.empty()) {
        auto stream = read_set.front();
        if (stream) {
            write_set.add(stream);
        }
        read_set.pop();
    }
    read_set.clear();

    // Switch the current set flag
    active_send_stream_set_1_is_current_ = !active_send_stream_set_1_is_current_;

    common::LOG_DEBUG("SwitchActiveSendStreamSet: switched to set %d", active_send_stream_set_1_is_current_ ? 1 : 2);
}

}  // namespace quic
}  // namespace quicx
