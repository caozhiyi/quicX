#include "quic/connection/controler/recv_flow_controller.h"

#include "common/log/log.h"

#include "quic/frame/max_data_frame.h"
#include "quic/frame/max_streams_frame.h"

namespace quicx {
namespace quic {

RecvFlowController::RecvFlowController()
    : received_bytes_(0),
      max_data_(0),
      max_streams_bidi_(0),
      max_streams_uni_(0),
      max_bidi_stream_id_(0),
      max_uni_stream_id_(0) {}

void RecvFlowController::UpdateConfig(const TransportParam& tp) {
    max_data_ = tp.GetInitialMaxData();
    max_streams_bidi_ = tp.GetInitialMaxStreamsBidi();
    max_streams_uni_ = tp.GetInitialMaxStreamsUni();

    common::LOG_DEBUG("RecvFlowController::UpdateConfig: max_data=%llu, max_streams_bidi=%llu, max_streams_uni=%llu",
        max_data_, max_streams_bidi_, max_streams_uni_);
}

bool RecvFlowController::OnDataReceived(uint32_t size) {
    received_bytes_ += size;
    common::LOG_DEBUG("RecvFlowController::OnDataReceived: received %u bytes, total=%llu, limit=%llu", size,
        received_bytes_, max_data_);

    // Check if peer exceeded our limit (protocol violation)
    if (received_bytes_ > max_data_) {
        common::LOG_ERROR("RecvFlowController::OnDataReceived: peer exceeded MAX_DATA limit (received=%llu, limit=%llu)",
            received_bytes_, max_data_);
        return false;
    }

    return true;
}

bool RecvFlowController::ShouldSendMaxData(std::shared_ptr<IFrame>& max_data_frame) {
    // Check if peer violated limit
    if (received_bytes_ > max_data_) {
        common::LOG_ERROR("RecvFlowController::ShouldSendMaxData: peer exceeded limit");
        return false;
    }

    // Check if we should increase the limit (peer is near the window)
    uint64_t remaining = max_data_ - received_bytes_;
    if (remaining <= kDataIncreaseThreshold) {
        // Increase the limit
        max_data_ += kDataIncreaseAmount;

        auto frame = std::make_shared<MaxDataFrame>();
        frame->SetMaximumData(max_data_);
        max_data_frame = frame;

        common::LOG_DEBUG(
            "RecvFlowController::ShouldSendMaxData: increasing limit to %llu (remaining was %llu, threshold=%llu)",
            max_data_, remaining, kDataIncreaseThreshold);
    }

    return true;
}

bool RecvFlowController::OnStreamCreated(uint64_t stream_id, std::shared_ptr<IFrame>& max_streams_frame) {
    // Determine stream direction from stream ID (bit 1: 0=bidirectional, 1=unidirectional)
    bool is_unidirectional = (stream_id & 0x02) != 0;

    if (is_unidirectional) {
        // Track highest unidirectional stream ID
        if (stream_id > max_uni_stream_id_) {
            max_uni_stream_id_ = stream_id;
        }
        return CheckUniStreamLimit(max_streams_frame);
    } else {
        // Track highest bidirectional stream ID
        if (stream_id > max_bidi_stream_id_) {
            max_bidi_stream_id_ = stream_id;
        }
        return CheckBidiStreamLimit(max_streams_frame);
    }
}

bool RecvFlowController::CheckBidiStreamLimit(std::shared_ptr<IFrame>& max_streams_frame) {
    // Convert stream ID to stream count (stream ID >> 2 gives the stream number)
    uint64_t current_stream_count = max_bidi_stream_id_ >> 2;

    // Check if peer exceeded our limit (protocol violation)
    // RFC 9000: MAX_STREAMS is the count, so if limit is 10, we can have streams 0-9
    if (current_stream_count >= max_streams_bidi_) {
        common::LOG_ERROR(
            "RecvFlowController::CheckBidiStreamLimit: peer exceeded MAX_STREAMS_BIDI limit (count=%llu, limit=%llu)",
            current_stream_count, max_streams_bidi_);
        return false;
    }

    // Check if we should increase the limit (peer is near the window)
    uint64_t remaining = max_streams_bidi_ - current_stream_count;
    if (remaining <= kStreamsIncreaseThreshold) {
        // Increase the limit
        max_streams_bidi_ += kStreamsIncreaseAmount;

        auto frame = std::make_shared<MaxStreamsFrame>(FrameType::kMaxStreamsBidirectional);
        frame->SetMaximumStreams(max_streams_bidi_);
        max_streams_frame = frame;

        common::LOG_DEBUG("RecvFlowController::CheckBidiStreamLimit: increasing limit to %llu (remaining was %llu, "
                          "threshold=%llu)",
            max_streams_bidi_, remaining, kStreamsIncreaseThreshold);
    }

    return true;
}

bool RecvFlowController::CheckUniStreamLimit(std::shared_ptr<IFrame>& max_streams_frame) {
    // Convert stream ID to stream count
    uint64_t current_stream_count = max_uni_stream_id_ >> 2;

    // Check if peer exceeded our limit (protocol violation)
    // RFC 9000: MAX_STREAMS is the count, so if limit is 10, we can have streams 0-9
    if (current_stream_count >= max_streams_uni_) {
        common::LOG_ERROR(
            "RecvFlowController::CheckUniStreamLimit: peer exceeded MAX_STREAMS_UNI limit (count=%llu, limit=%llu)",
            current_stream_count, max_streams_uni_);
        return false;
    }

    // Check if we should increase the limit (peer is near the window)
    uint64_t remaining = max_streams_uni_ - current_stream_count;
    if (remaining <= kStreamsIncreaseThreshold) {
        // Increase the limit
        max_streams_uni_ += kStreamsIncreaseAmount;

        auto frame = std::make_shared<MaxStreamsFrame>(FrameType::kMaxStreamsUnidirectional);
        frame->SetMaximumStreams(max_streams_uni_);
        max_streams_frame = frame;

        common::LOG_DEBUG("RecvFlowController::CheckUniStreamLimit: increasing limit to %llu (remaining was %llu, "
                          "threshold=%llu)",
            max_streams_uni_, remaining, kStreamsIncreaseThreshold);
    }

    return true;
}

}  // namespace quic
}  // namespace quicx
