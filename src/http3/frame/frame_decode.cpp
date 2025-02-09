#include <functional>
#include <unordered_map>
#include "common/util/singleton.h"
#include "http3/frame/data_frame.h"
#include "http3/frame/frame_decode.h"
#include "http3/frame/goaway_frame.h"
#include "http3/frame/headers_frame.h"
#include "http3/frame/settings_frame.h"
#include "http3/frame/max_push_id_frame.h"
#include "http3/frame/cancel_push_frame.h"
#include "http3/frame/push_promise_frame.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace http3 {

static const std::unordered_map<uint16_t, std::function<std::shared_ptr<IFrame>()>> kFrameCreaterMap = {
    {static_cast<uint16_t>(FrameType::kData),         []() -> std::shared_ptr<IFrame> { return std::make_shared<DataFrame>(); }},
    {static_cast<uint16_t>(FrameType::kHeaders),      []() -> std::shared_ptr<IFrame> { return std::make_shared<HeadersFrame>(); }},
    {static_cast<uint16_t>(FrameType::kCancelPush),   []() -> std::shared_ptr<IFrame> { return std::make_shared<CancelPushFrame>(); }},
    {static_cast<uint16_t>(FrameType::kSettings),     []() -> std::shared_ptr<IFrame> { return std::make_shared<SettingsFrame>(); }},
    {static_cast<uint16_t>(FrameType::kPushPromise),  []() -> std::shared_ptr<IFrame> { return std::make_shared<PushPromiseFrame>(); }},
    {static_cast<uint16_t>(FrameType::kGoaway),       []() -> std::shared_ptr<IFrame> { return std::make_shared<GoawayFrame>(); }},
    {static_cast<uint16_t>(FrameType::kMaxPushId),    []() -> std::shared_ptr<IFrame> { return std::make_shared<MaxPushIdFrame>(); }},
};

bool DecodeFrames(std::shared_ptr<common::IBufferRead> buffer, std::vector<std::shared_ptr<IFrame>>& frames) {
    if(buffer->GetDataLength() == 0) {
        return false;
    }

    while(buffer->GetDataLength() > 0) {
        common::BufferDecodeWrapper wrapper(buffer);
        uint16_t frame_type = 0;
        if(!wrapper.DecodeFixedUint16(frame_type)) {
            return false;
        }
        wrapper.Flush();

        auto creater = kFrameCreaterMap.find(frame_type);
        if(creater == kFrameCreaterMap.end()) {
            return false;
        }

        std::shared_ptr<IFrame> frame = creater->second();
        if(!frame->Decode(buffer)) {
            return false;
        }

        frames.push_back(frame);
    }

    return true;
}

}
}
