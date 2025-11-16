#ifndef HTTP3_FRAME_SETTINGS_FRAME
#define HTTP3_FRAME_SETTINGS_FRAME

#include <unordered_map>
#include "http3/frame/type.h"
#include "http3/frame/if_frame.h"

namespace quicx {
namespace http3 {

class SettingsFrame:
    public IFrame {
public:
    SettingsFrame(): IFrame(FrameType::kSettings), length_(0) {}

    const std::unordered_map<uint16_t, uint64_t>& GetSettings() const { return settings_; }
    void SetSetting(uint16_t id, uint64_t value) { settings_[id] = value; }
    bool GetSetting(uint16_t id, uint64_t& value);

    bool Encode(std::shared_ptr<common::IBuffer> buffer);
    bool Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type = false);
    uint32_t EvaluateEncodeSize();
    uint32_t EvaluatePayloadSize();

private:
    uint64_t length_;
    std::unordered_map<uint16_t, uint64_t> settings_;
};

}
}

#endif 