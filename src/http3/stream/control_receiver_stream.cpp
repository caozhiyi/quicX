#include "common/log/log.h"
#include "http3/http/error.h"
#include "http3/frame/frame_decode.h"
#include "http3/frame/goaway_frame.h"
#include "http3/frame/settings_frame.h"
#include "http3/stream/control_receiver_stream.h"

namespace quicx {
namespace http3 {

ControlReceiverStream::ControlReceiverStream(const std::shared_ptr<quic::IQuicRecvStream>& stream,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
    const std::function<void(uint64_t id)>& goaway_handler,
    const std::function<void(const std::unordered_map<uint16_t, uint64_t>& settings)>& settings_handler):
    IStream(error_handler),
    stream_(stream),
    goaway_handler_(goaway_handler),
    settings_handler_(settings_handler) {
}

ControlReceiverStream::~ControlReceiverStream() {
    if (stream_) {
        stream_->Close();
    }
}

void ControlReceiverStream::OnData(std::shared_ptr<common::IBufferRead> data, uint32_t error) {
    if (error != 0) {
        common::LOG_ERROR("IStream::OnData error: %d", error);
        error_handler_(stream_->GetStreamID(), error);
        return;
    }

    std::vector<std::shared_ptr<IFrame>> frames;
    if (!DecodeFrames(data, frames)) {
        common::LOG_ERROR("IStream::OnData decode frames error");
        error_handler_(stream_->GetStreamID(), HTTP3_ERROR_CODE::H3EC_MESSAGE_ERROR);
        return;
    }

    for (const auto& frame : frames) {
        HandleFrame(frame);
    }
}

void ControlReceiverStream::HandleFrame(std::shared_ptr<IFrame> frame) {
    switch (frame->GetType()) {
        case FT_GOAWAY: {
            auto goaway_frame = std::static_pointer_cast<GoawayFrame>(frame);
            goaway_handler_(goaway_frame->GetStreamId());
            break;
        }
        case FT_SETTINGS: {
            auto settings_frame = std::static_pointer_cast<SettingsFrame>(frame);
            settings_handler_(settings_frame->GetSettings());
            break;
        }
        default:
            common::LOG_ERROR("IStream::OnData unknown frame type: %d", frame->GetType());
            error_handler_(stream_->GetStreamID(), HTTP3_ERROR_CODE::H3EC_FRAME_UNEXPECTED);
            break;
    }
}

}
}
