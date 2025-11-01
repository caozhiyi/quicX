#include "common/log/log.h"
#include "http3/http/error.h"
#include "http3/frame/frame_decode.h"
#include "http3/frame/goaway_frame.h"
#include "http3/frame/settings_frame.h"
#include "http3/stream/control_receiver_stream.h"
#include "http3/qpack/qpack_encoder.h"

namespace quicx {
namespace http3 {

ControlReceiverStream::ControlReceiverStream(const std::shared_ptr<quic::IQuicRecvStream>& stream,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
    const std::function<void(uint64_t id)>& goaway_handler,
    const std::function<void(const std::unordered_map<uint16_t, uint64_t>& settings)>& settings_handler):
    IRecvStream(error_handler),
    stream_(stream),
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

void ControlReceiverStream::OnData(std::shared_ptr<common::IBufferRead> data, uint32_t error) {
    if (error != 0) {
        common::LOG_ERROR("IRecvStream::OnData error: %d", error);
        error_handler_(stream_->GetStreamID(), error);
        return;
    }

    // Try parse HTTP/3 frames; if fails, treat as raw QPACK instruction blob (demo)
    std::vector<std::shared_ptr<IFrame>> frames;
    auto snapshot = data; // shallow copy
    if (DecodeFrames(snapshot, frames)) {
        for (const auto& frame : frames) {
            HandleFrame(frame);
        }
    } else {
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

void ControlReceiverStream::HandleRawData(std::shared_ptr<common::IBufferRead> data) {
    // Interpret as QPACK encoder instructions and update dynamic table
    QpackEncoder enc;
    enc.DecodeEncoderInstructions(data);
}

}
}
