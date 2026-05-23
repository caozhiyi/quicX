#include "quic/connection/controler/send_flow_controller.h"

#include "common/log/log.h"

#include "quic/config.h"
#include "quic/frame/data_blocked_frame.h"
#include "quic/frame/streams_blocked_frame.h"

namespace quicx {
namespace quic {

SendFlowController::SendFlowController(StreamIDGenerator::StreamStarter starter)
    : sent_bytes_(0),
      max_data_(0),
      max_streams_bidi_(0),
      max_streams_uni_(0),
      max_bidi_stream_id_(0),
      max_uni_stream_id_(0),
      id_generator_(starter) {}

void SendFlowController::UpdateConfig(const TransportParam& tp) {
    max_data_ = tp.GetInitialMaxData();
    max_streams_bidi_ = tp.GetInitialMaxStreamsBidi();
    max_streams_uni_ = tp.GetInitialMaxStreamsUni();

    common::LOG_INFO("SendFlowController::UpdateConfig: max_data=%llu, max_streams_bidi=%llu, max_streams_uni=%llu",
        max_data_, max_streams_bidi_, max_streams_uni_);
}

void SendFlowController::OnDataSent(uint32_t size) {
    sent_bytes_ += size;
    common::LOG_DEBUG("SendFlowController::OnDataSent: sent %u bytes, total=%llu, limit=%llu", size, sent_bytes_,
        max_data_);
}

void SendFlowController::OnMaxDataReceived(uint64_t limit) {
    if (limit > max_data_) {
        common::LOG_INFO(
            "SendFlowController::OnMaxDataReceived: increasing limit from %llu to %llu", max_data_, limit);
        max_data_ = limit;
        // Limit increased -> peer might block us at the new limit later; reset
        // de-dup so a fresh DATA_BLOCKED can be emitted if we hit the new wall.
        last_data_blocked_limit_ = 0;
    } else {
        // RFC 9000 Section 4.1: Ignore frames that don't increase limits
        common::LOG_INFO("SendFlowController::OnMaxDataReceived: ignoring non-increasing limit %llu (current=%llu)",
            limit, max_data_);
    }
}

bool SendFlowController::CanSendData(uint64_t& can_send_size, std::shared_ptr<IFrame>& blocked_frame) {
    // Check if we've reached the flow control limit
    if (sent_bytes_ >= max_data_) {
        // Blocked: cannot send any data.
        // RFC 9000 §19.12: DATA_BLOCKED is purely informational ("the sender
        // wishes to send data, but is unable to due to connection-level flow
        // control"). Emitting one frame per max_data_ value is enough; the
        // peer will respond with MAX_DATA when it can. Without de-dup the
        // server keeps minting DATA_BLOCKEDs every time CanSendData() is
        // invoked, which under interop with quic-go/quiche caused PN to
        // explode past 500k while no real data flowed.
        if (last_data_blocked_limit_ != max_data_) {
            auto frame = std::make_shared<DataBlockedFrame>();
            frame->SetMaximumData(max_data_);
            blocked_frame = frame;
            last_data_blocked_limit_ = max_data_;
            common::LOG_DEBUG("SendFlowController::CanSendData: BLOCKED at limit %llu (DATA_BLOCKED emitted)", max_data_);
        } else {
            common::LOG_DEBUG("SendFlowController::CanSendData: BLOCKED at limit %llu (suppressed duplicate DATA_BLOCKED)", max_data_);
        }
        can_send_size = 0;
        return false;
    }

    // Calculate available send window
    can_send_size = max_data_ - sent_bytes_;

    // Check if we're near the limit (proactive signaling)
    if (can_send_size <= kDataBlockedThreshold) {
        if (last_data_blocked_limit_ != max_data_) {
            auto frame = std::make_shared<DataBlockedFrame>();
            frame->SetMaximumData(max_data_);
            blocked_frame = frame;
            last_data_blocked_limit_ = max_data_;
            common::LOG_DEBUG(
                "SendFlowController::CanSendData: near limit, can_send=%llu, threshold=%llu (DATA_BLOCKED emitted)",
                can_send_size, kDataBlockedThreshold);
        }
    }

    return true;
}

void SendFlowController::OnMaxStreamsBidiReceived(uint64_t limit) {
    if (limit > max_streams_bidi_) {
        common::LOG_DEBUG(
            "SendFlowController::OnMaxStreamsBidiReceived: increasing from %llu to %llu", max_streams_bidi_, limit);
        max_streams_bidi_ = limit;
    } else {
        // RFC 9000 Section 4.1: Ignore non-increasing limits
        common::LOG_DEBUG(
            "SendFlowController::OnMaxStreamsBidiReceived: ignoring non-increasing limit %llu (current=%llu)", limit,
            max_streams_bidi_);
    }
}

bool SendFlowController::CanCreateBidiStream(uint64_t& stream_id, std::shared_ptr<IFrame>& blocked_frame) {
    // Peek at next stream ID without committing
    stream_id = id_generator_.PeekNextStreamID(StreamIDGenerator::StreamDirection::kBidirectional);

    // Convert stream ID to stream count (stream ID >> 2 gives the stream number)
    uint64_t next_stream_count = stream_id >> 2;

    // Check if creating this stream would exceed the limit
    // RFC 9000: MAX_STREAMS is the count, so if limit is 10, we can have streams 0-9
    if (next_stream_count >= max_streams_bidi_) {
        // Blocked: cannot create stream
        auto frame = std::make_shared<StreamsBlockedFrame>(FrameType::kStreamsBlockedBidirectional);
        frame->SetMaximumStreams(max_streams_bidi_);
        blocked_frame = frame;
        common::LOG_DEBUG(
            "SendFlowController::CanCreateBidiStream: BLOCKED at limit %llu (next would be %llu)", max_streams_bidi_,
            next_stream_count);
        return false;
    }

    // Allowed: allocate the stream ID
    stream_id = id_generator_.NextStreamID(StreamIDGenerator::StreamDirection::kBidirectional);
    max_bidi_stream_id_ = stream_id;

    // Check if we're near the limit (proactive signaling)
    uint64_t remaining = max_streams_bidi_ - (stream_id >> 2);
    if (remaining <= kStreamsBlockedThreshold) {
        auto frame = std::make_shared<StreamsBlockedFrame>(FrameType::kStreamsBlockedBidirectional);
        frame->SetMaximumStreams(max_streams_bidi_);
        blocked_frame = frame;
        common::LOG_DEBUG(
            "SendFlowController::CanCreateBidiStream: near limit, remaining=%llu, threshold=%llu", remaining,
            kStreamsBlockedThreshold);
    }

    common::LOG_DEBUG("SendFlowController::CanCreateBidiStream: allocated stream %llu, count=%llu, limit=%llu",
        stream_id, stream_id >> 2, max_streams_bidi_);
    return true;
}

void SendFlowController::OnMaxStreamsUniReceived(uint64_t limit) {
    if (limit > max_streams_uni_) {
        common::LOG_DEBUG(
            "SendFlowController::OnMaxStreamsUniReceived: increasing from %llu to %llu", max_streams_uni_, limit);
        max_streams_uni_ = limit;
    } else {
        common::LOG_DEBUG(
            "SendFlowController::OnMaxStreamsUniReceived: ignoring non-increasing limit %llu (current=%llu)", limit,
            max_streams_uni_);
    }
}

bool SendFlowController::CanCreateUniStream(uint64_t& stream_id, std::shared_ptr<IFrame>& blocked_frame) {
    // Peek at next stream ID without committing
    stream_id = id_generator_.PeekNextStreamID(StreamIDGenerator::StreamDirection::kUnidirectional);

    // Convert stream ID to stream count
    uint64_t next_stream_count = stream_id >> 2;

    // Check if creating this stream would exceed the limit
    // RFC 9000: MAX_STREAMS is the count, so if limit is 10, we can have streams 0-9
    if (next_stream_count >= max_streams_uni_) {
        // Blocked: cannot create stream
        auto frame = std::make_shared<StreamsBlockedFrame>(FrameType::kStreamsBlockedUnidirectional);
        frame->SetMaximumStreams(max_streams_uni_);
        blocked_frame = frame;
        common::LOG_DEBUG(
            "SendFlowController::CanCreateUniStream: BLOCKED at limit %llu (next would be %llu)", max_streams_uni_,
            next_stream_count);
        return false;
    }

    // Allowed: allocate the stream ID
    stream_id = id_generator_.NextStreamID(StreamIDGenerator::StreamDirection::kUnidirectional);
    max_uni_stream_id_ = stream_id;

    // Check if we're near the limit (proactive signaling)
    uint64_t remaining = max_streams_uni_ - (stream_id >> 2);
    if (remaining <= kStreamsBlockedThreshold) {
        auto frame = std::make_shared<StreamsBlockedFrame>(FrameType::kStreamsBlockedUnidirectional);
        frame->SetMaximumStreams(max_streams_uni_);
        blocked_frame = frame;
        common::LOG_DEBUG("SendFlowController::CanCreateUniStream: near limit, remaining=%llu, threshold=%llu",
            remaining, kStreamsBlockedThreshold);
    }

    common::LOG_DEBUG("SendFlowController::CanCreateUniStream: allocated stream %llu, count=%llu, limit=%llu",
        stream_id, stream_id >> 2, max_streams_uni_);
    return true;
}

}  // namespace quic
}  // namespace quicx
