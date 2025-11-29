#include "common/decode/decode.h"
#include "http3/frame/settings_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/multi_block_buffer_decode_wrapper.h"

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

bool SettingsFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
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

DecodeResult SettingsFrame::Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type) {
    common::MultiBlockBufferDecodeWrapper wrapper(buffer);
    if (with_type) {
        if (!wrapper.DecodeFixedUint16(type_)) {
            return DecodeResult::kNeedMoreData;
        }
    }

    if (!wrapper.DecodeVarint(length_)) {
        return DecodeResult::kNeedMoreData;
    }

    if (length_ == 0) {
        wrapper.Flush();
        return DecodeResult::kSuccess;
    }

    // Check if we have enough data for all settings
    if (wrapper.GetDataLength() < length_) {
        wrapper.CancelDecode();
        return DecodeResult::kNeedMoreData;
    }

    int32_t len = (int32_t)length_;
    while (len > 0) {  // TODO: check max loop times
        uint64_t id, value;
        if (!wrapper.DecodeVarint(id, len) || !wrapper.DecodeVarint(value, len)) {
            // If we can't decode, check if it's because we need more data
            if (wrapper.GetDataLength() == 0) {
                wrapper.CancelDecode();
                return DecodeResult::kNeedMoreData;
            }
            return DecodeResult::kError;
        }
        settings_[id] = value;
    }
    wrapper.Flush();
    return DecodeResult::kSuccess;
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

}  // namespace http3
}  // namespace quicx