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

class FrameDecode:
    public common::Singleton<FrameDecode> {
public:
    FrameDecode();
    ~FrameDecode();

    bool DecodeFrames(std::shared_ptr<common::IBufferRead> buffer, std::vector<std::shared_ptr<IFrame>>& frames);
private:
    typedef std::function<std::shared_ptr<IFrame>()> FrameCreater;
    // frame type to craeter function map
    static std::unordered_map<uint16_t, FrameCreater> __frame_creater_map;
};

std::unordered_map<uint16_t, FrameDecode::FrameCreater> FrameDecode::__frame_creater_map;

FrameDecode::FrameDecode() {
    __frame_creater_map[FT_DATA]                = []() -> std::shared_ptr<IFrame> { return std::make_shared<DataFrame>(); };
    __frame_creater_map[FT_HEADERS]             = []() -> std::shared_ptr<IFrame> { return std::make_shared<HeadersFrame>(); };
    __frame_creater_map[FT_CANCEL_PUSH]         = []() -> std::shared_ptr<IFrame> { return std::make_shared<CancelPushFrame>(); };
    __frame_creater_map[FT_SETTINGS]            = []() -> std::shared_ptr<IFrame> { return std::make_shared<SettingsFrame>(); };
    __frame_creater_map[FT_PUSH_PROMISE]        = []() -> std::shared_ptr<IFrame> { return std::make_shared<PushPromiseFrame>(); };
    __frame_creater_map[FT_GOAWAY]              = []() -> std::shared_ptr<IFrame> { return std::make_shared<GoAwayFrame>(); };
    __frame_creater_map[FT_MAX_PUSH_ID]         = []() -> std::shared_ptr<IFrame> { return std::make_shared<MaxPushIdFrame>(); };
}

FrameDecode::~FrameDecode() {

}

bool FrameDecode::DecodeFrames(std::shared_ptr<common::IBufferRead> buffer, std::vector<std::shared_ptr<IFrame>>& frames) {
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

        auto creater = __frame_creater_map.find(frame_type);
        if(creater == __frame_creater_map.end()) {
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

bool DecodeFrames(std::shared_ptr<common::IBufferRead> buffer, std::vector<std::shared_ptr<IFrame>>& frames) {
    return FrameDecode::Instance().DecodeFrames(buffer, frames);
}

}
}
