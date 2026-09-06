#include "common/log/log.h"

#include "http3/frame/goaway_frame.h"
#include "http3/frame/settings_frame.h"
#include "http3/http/error.h"
#include "http3/stream/control_sender_stream.h"
#include "http3/stream/type.h"

namespace quicx {
namespace http3 {

ControlSenderStream::ControlSenderStream(const std::shared_ptr<IQuicSendStream>& stream,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler):
    ISendStream(StreamType::kControl, stream, error_handler) {}

ControlSenderStream::~ControlSenderStream() {
    // Note: Do NOT call stream_->Close() here during destruction.
    // See QpackDecoderSenderStream destructor comment for details.
    stream_.reset();
}

bool ControlSenderStream::SendSettings(const std::unordered_map<uint16_t, uint64_t>& settings) {
    if (!EnsureStreamPreamble()) {
        return false;
    }

    SettingsFrame frame;
    for (const auto& setting : settings) {
        frame.SetSetting(static_cast<uint16_t>(setting.first), setting.second);
    }

    if (!EncodeAndAppendControlFrame([&frame](std::shared_ptr<common::IBuffer> buf) { return frame.Encode(buf); })) {
        LOG_ERROR("ControlSenderStream::SendSettings: Failed to encode SettingsFrame");
        error_handler_(stream_->GetStreamID(), Http3ErrorCode::kMessageError);
        return false;
    }
    return true;
}

bool ControlSenderStream::SendGoaway(uint64_t id) {
    if (!EnsureStreamPreamble()) {
        return false;
    }

    GoAwayFrame frame;
    frame.SetStreamId(id);

    if (!EncodeAndAppendControlFrame([&frame](std::shared_ptr<common::IBuffer> buf) { return frame.Encode(buf); })) {
        LOG_ERROR("ControlSenderStream::SendGoaway: Failed to encode GoAwayFrame");
        error_handler_(stream_->GetStreamID(), Http3ErrorCode::kInternalError);
        return false;
    }
    return true;
}

bool ControlSenderStream::SendQpackInstructions(const std::vector<uint8_t>& blob) {
    if (!EnsureStreamPreamble()) {
        return false;
    }

    if (blob.empty()) {
        return true;
    }
    return stream_->Send((uint8_t*)blob.data(), blob.size()) > 0;
}

}  // namespace http3
}  // namespace quicx
