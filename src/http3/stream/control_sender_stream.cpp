#include "common/log/log.h"
#include "http3/http/error.h"
#include "common/buffer/buffer.h"
#include "http3/frame/goaway_frame.h"
#include "http3/frame/settings_frame.h"
#include "http3/frame/max_push_id_frame.h"
#include "http3/frame/cancel_push_frame.h"
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
        frame.SetSetting(setting.first, setting.second);
    }

    uint8_t buf[1024]; // TODO: Use dynamic buffer
    auto buffer = std::make_shared<common::Buffer>(buf, sizeof(buf));
    if (!frame.Encode(buffer)) {
        common::LOG_ERROR("ControlSenderStream::SendSettings: Failed to encode SettingsFrame");
        error_handler_(stream_->GetStreamID(), HTTP3_ERROR_CODE::H3EC_MESSAGE_ERROR);
        return false;
    }
    
    return stream_->Send(buffer) > 0;
}

bool ControlSenderStream::SendGoaway(uint64_t id) {
    GoawayFrame frame;
    frame.SetStreamId(id);
    
    uint8_t buf[1024]; // TODO: Use dynamic buffer
    auto buffer = std::make_shared<common::Buffer>(buf, sizeof(buf));
    if (!frame.Encode(buffer)) {
        common::LOG_ERROR("ControlSenderStream::SendGoaway: Failed to encode GoawayFrame");
        error_handler_(stream_->GetStreamID(), HTTP3_ERROR_CODE::H3EC_INTERNAL_ERROR);
        return false;
    }
    return stream_->Send(buffer) > 0;
}

}
}
