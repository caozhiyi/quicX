#include "common/decode/decode.h"
#include "http3/frame/settings_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace http3 {

bool SettingsFrame::GetSetting(uint16_t id, uint64_t& value) {
    auto it = settings_.find(id);
    if (it == settings_.end()) {
        return false;
    }
    value = it->second;
    return true;
}

bool SettingsFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    if (buffer->GetFreeLength() < EvaluateEncodeSize()) {
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    // Write the frame type
    if (!wrapper.EncodeFixedUint16(GetType())) {
        return false;
    }

    // Write the number of settings
    if (!wrapper.EncodeVarint(EvaluatePayloadSize())) {
        return false;
    }

    // Write each setting
    for (const auto& setting : settings_) {
        if (!wrapper.EncodeVarint(setting.first) || !wrapper.EncodeVarint(setting.second)) {
            return false;
        }
    }
    return true;
}

bool SettingsFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);
    if (with_type) {
        if (!wrapper.DecodeFixedUint16(type_)) {
            return false;
        }
    }   

    if (!wrapper.DecodeVarint(length_)) {
        return false;
    }   

    if (length_ == 0) {
        return true;
    }

    int32_t len = (int32_t)length_;
    while (len > 0) { // TODO: check max loop times
        uint64_t id, value;
        if (!wrapper.DecodeVarint(id, len) || !wrapper.DecodeVarint(value, len)) {
            return false;
        }
        settings_[id] = value;
    }
    return true;
}

uint32_t SettingsFrame::EvaluateEncodeSize() {
    uint32_t size = 0;

    // Size for the frame type
    size += sizeof(type_);

    // Size for each setting (id and value)
    size += EvaluatePayloadSize(); 

    // Size for the number of settings
    size += common::GetEncodeVarintLength(length_);
    return size;
}

uint32_t SettingsFrame::EvaluatePayloadSize() {
    if (length_ == 0) {
        // Size for each setting (id and value)
        for (const auto& setting : settings_) {
            // Calculate varint size for id
            length_ += common::GetEncodeVarintLength(setting.first);
            length_ += common::GetEncodeVarintLength(setting.second);
        }
    }

    return length_;
}

}
}