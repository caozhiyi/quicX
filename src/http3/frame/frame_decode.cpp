#include <functional>
#include <unordered_map>

#include "common/log/log.h"
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

static const std::unordered_map<uint16_t, std::function<std::shared_ptr<IFrame>()>> kFrameCreatorMap = {
    {FrameType::kData,         []() -> std::shared_ptr<IFrame> { return std::make_shared<DataFrame>(); }},
    {FrameType::kHeaders,      []() -> std::shared_ptr<IFrame> { return std::make_shared<HeadersFrame>(); }},
    {FrameType::kCancelPush,   []() -> std::shared_ptr<IFrame> { return std::make_shared<CancelPushFrame>(); }},
    {FrameType::kSettings,     []() -> std::shared_ptr<IFrame> { return std::make_shared<SettingsFrame>(); }},
    {FrameType::kPushPromise,  []() -> std::shared_ptr<IFrame> { return std::make_shared<PushPromiseFrame>(); }},
    {FrameType::kGoAway,       []() -> std::shared_ptr<IFrame> { return std::make_shared<GoAwayFrame>(); }},
    {FrameType::kMaxPushId,    []() -> std::shared_ptr<IFrame> { return std::make_shared<MaxPushIdFrame>(); }},
};

bool DecodeFrames(std::shared_ptr<common::IBuffer> buffer, std::vector<std::shared_ptr<IFrame>>& frames) {
    if(buffer->GetDataLength() == 0) {
        common::LOG_DEBUG("DecodeFrames: buffer is empty");
        return false;
    }

    uint32_t initial_length = buffer->GetDataLength();
    common::LOG_DEBUG("DecodeFrames: starting decode, buffer length=%u", initial_length);

    while(buffer->GetDataLength() > 0) {
        uint32_t length_before = buffer->GetDataLength();
        
        common::BufferDecodeWrapper wrapper(buffer);
        uint16_t frame_type = 0;
        if(!wrapper.DecodeFixedUint16(frame_type)) {
            common::LOG_ERROR("DecodeFrames: failed to decode frame type, remaining=%u", buffer->GetDataLength());
            return false;
        }
        wrapper.Flush();

        common::LOG_DEBUG("DecodeFrames: decoded frame type=%u (0x%04x), remaining=%u", frame_type, frame_type, buffer->GetDataLength());

        auto creator = kFrameCreatorMap.find(frame_type);
        if(creator == kFrameCreatorMap.end()) {
            common::LOG_ERROR("DecodeFrames: unknown frame type=%u (0x%04x)", frame_type, frame_type);
            return false;
        }

        std::shared_ptr<IFrame> frame = creator->second();
        uint32_t length_before_decode = buffer->GetDataLength();
        if(!frame->Decode(buffer, false)) {
            common::LOG_ERROR("DecodeFrames: failed to decode frame type=%u, remaining=%u", frame_type, buffer->GetDataLength());
            return false;
        }
        uint32_t length_after_decode = buffer->GetDataLength();
        uint32_t consumed = length_before_decode - length_after_decode;
        common::LOG_DEBUG("DecodeFrames: successfully decoded frame type=%u, consumed=%u bytes, remaining=%u", 
                        frame_type, consumed, buffer->GetDataLength());

        frames.push_back(frame);
        
        // Safety check: ensure we consumed some data
        if (buffer->GetDataLength() >= length_before) {
            common::LOG_ERROR("DecodeFrames: no progress made decoding frame type=%u, length_before=%u, length_after=%u", 
                            frame_type, length_before, buffer->GetDataLength());
            return false;
        }
    }

    common::LOG_DEBUG("DecodeFrames: completed, decoded %zu frames, consumed %u bytes total", 
                     frames.size(), initial_length);
    return true;
}

}
}
