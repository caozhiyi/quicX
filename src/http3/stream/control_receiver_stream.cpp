#include "common/log/log.h"
#include "http3/http/error.h"
#include "http3/frame/frame_decode.h"
#include "http3/frame/goaway_frame.h"
#include "http3/frame/settings_frame.h"
#include "http3/stream/control_receiver_stream.h"

namespace quicx {
namespace http3 {

ControlReceiverStream::ControlReceiverStream(const std::shared_ptr<IQuicRecvStream>& stream,
    const std::shared_ptr<QpackEncoder>& qpack_encoder,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
    const std::function<void(uint64_t id)>& goaway_handler,
    const std::function<void(const std::unordered_map<uint16_t, uint64_t>& settings)>& settings_handler):
    IRecvStream(StreamType::kControl, stream, error_handler),
    goaway_handler_(goaway_handler),
    settings_handler_(settings_handler) {

    stream_->SetStreamReadCallBack(std::bind(&ControlReceiverStream::OnData, this, std::placeholders::_1, std::placeholders::_2));
    // TODO send callback
}

ControlReceiverStream::~ControlReceiverStream() {
    if (stream_) {
        stream_->Reset(0);
    }
}

void ControlReceiverStream::OnData(std::shared_ptr<IBufferRead> data, uint32_t error) {
    if (error != 0) {
        common::LOG_ERROR("IRecvStream::OnData error: %d", error);
        error_handler_(stream_->GetStreamID(), error);
        return;
    }
    size_t total_length = data->GetDataLength();
    common::LOG_DEBUG("ControlReceiverStream::OnData: data length=%zu", total_length);

    // If buffer is empty (e.g., stream closed with FIN), nothing to do
    if (total_length == 0) {
        common::LOG_DEBUG("ControlReceiverStream::OnData: empty buffer, stream likely closed");
        return;
    }

    // Try parse HTTP/3 frames from unparsed data
    std::vector<std::shared_ptr<IFrame>> frames;
    size_t length_before = data->GetDataLength();
    common::LOG_DEBUG("ControlReceiverStream::OnData: before DecodeFrames, length_before=%zu", length_before);
    if (DecodeFrames(std::dynamic_pointer_cast<common::IBuffer>(data), frames)) {
        size_t length_after = data->GetDataLength();
        // DecodeFrames consumes frame data including the 2-byte frame type for each frame.
        // The consumed length is calculated from buffer length difference.
        size_t consumed = length_before - length_after;
        
        common::LOG_DEBUG("ControlReceiverStream::OnData: DecodeFrames succeeded, decoded %zu frames, length_before=%zu, length_after=%zu, consumed=%zu bytes", 
                         frames.size(), length_before, length_after, consumed);
        for (const auto& frame : frames) {
            common::LOG_DEBUG("ControlReceiverStream::OnData: handling frame type=%d", frame->GetType());
            HandleFrame(frame);
        }

    } else {
        common::LOG_DEBUG("ControlReceiverStream::OnData: DecodeFrames failed, treating as raw QPACK instructions");
        HandleRawData(data);
    }
}

void ControlReceiverStream::HandleFrame(std::shared_ptr<IFrame> frame) {
    switch (frame->GetType()) {
        case FrameType::kGoAway: {
            auto goaway_frame = std::static_pointer_cast<GoAwayFrame>(frame);
            goaway_handler_(goaway_frame->GetStreamId());
            break;
        }
        case FrameType::kSettings: {
            auto settings_frame = std::static_pointer_cast<SettingsFrame>(frame);
            settings_handler_(settings_frame->GetSettings());
            break;
        }
        default:
            common::LOG_ERROR("IStream::OnData unknown frame type: %d", frame->GetType());
            error_handler_(stream_->GetStreamID(), Http3ErrorCode::kFrameUnexpected);
            break;
    }
}

void ControlReceiverStream::HandleRawData(std::shared_ptr<IBufferRead> data) {
    // Interpret as QPACK encoder instructions and update dynamic table
    qpack_encoder_->DecodeEncoderInstructions(std::dynamic_pointer_cast<common::IBuffer>(data));
}

}
}
