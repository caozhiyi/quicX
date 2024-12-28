#ifndef HTTP3_FRAME_SETTINGS_FRAME
#define HTTP3_FRAME_SETTINGS_FRAME

#include <map>
#include "http3/frame/type.h"
#include "http3/frame/if_frame.h"

namespace quicx {
namespace http3 {

class SettingsFrame:
    public IFrame {
public:
    SettingsFrame(): IFrame(FT_SETTINGS), length_(0) {}

    const std::map<uint16_t, uint64_t>& GetSettings() const { return settings_; }
    void SetSetting(uint16_t id, uint64_t value) { settings_[id] = value; }
    bool GetSetting(uint16_t id, uint64_t& value);

    bool Encode(std::shared_ptr<common::IBufferWrite> buffer);
    bool Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type = false);
    uint32_t EvaluateEncodeSize();
    uint32_t EvaluatePaloadSize();

private:
    uint64_t length_;
    std::map<uint16_t, uint64_t> settings_;
};

}
}

#endif 