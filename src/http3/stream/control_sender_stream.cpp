#include "common/log/log.h"
#include "http3/http/error.h"
#include "common/buffer/buffer.h"
#include "http3/frame/goaway_frame.h"
#include "http3/frame/settings_frame.h"
#include "http3/frame/settings_frame.h"
#include "http3/stream/control_sender_stream.h"

namespace quicx {
namespace http3 {

ControlSenderStream::ControlSenderStream(const std::shared_ptr<quic::IQuicSendStream>& stream,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler):
    IStream(error_handler),
    stream_(stream) {
}

ControlSenderStream::~ControlSenderStream() {
    if (stream_) {
        stream_->Close();
    }
}

bool ControlSenderStream::SendSettings(const std::unordered_map<uint16_t, uint64_t>& settings) {
    SettingsFrame frame;
    for (const auto& setting : settings) {
        frame.SetSetting(static_cast<uint16_t>(setting.first), setting.second);
    }

    uint8_t buf[1024]; // TODO: Use dynamic buffer
    auto buffer = std::make_shared<common::Buffer>(buf, sizeof(buf));
    if (!frame.Encode(buffer)) {
        common::LOG_ERROR("ControlSenderStream::SendSettings: Failed to encode SettingsFrame");
        error_handler_(stream_->GetStreamID(), Http3ErrorCode::kMessageError);
        return false;
    }
    
    return stream_->Send(buffer) > 0;
}

bool ControlSenderStream::SendGoaway(uint64_t id) {
    GoAwayFrame frame;
    frame.SetStreamId(id);
    
    uint8_t buf[1024]; // TODO: Use dynamic buffer
    auto buffer = std::make_shared<common::Buffer>(buf, sizeof(buf));
    if (!frame.Encode(buffer)) {
        common::LOG_ERROR("ControlSenderStream::SendGoaway: Failed to encode GoAwayFrame");
        error_handler_(stream_->GetStreamID(), Http3ErrorCode::kInternalError);
        return false;
    }
    return stream_->Send(buffer) > 0;
}

bool ControlSenderStream::SendQpackInstructions(const std::vector<uint8_t>& blob) {
    if (blob.empty()) return true;
    auto buffer = std::make_shared<common::Buffer>((uint8_t*)blob.data(), blob.size());
    buffer->MoveWritePt(blob.size());
    return stream_->Send(buffer) > 0;
}

}
}
