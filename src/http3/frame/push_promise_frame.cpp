#include "http3/frame/push_promise_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace http3 {

bool PushPromiseFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    if (buffer->GetFreeLength() < EvaluateEncodeSize()) {
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    // Write frame type
    if (!wrapper.EncodeFixedUint16(type_)) {
        return false;
    }

    // Write length
    if (!wrapper.EncodeVarint(EvaluatePayloadSize())) {
        return false;
    }

    // Write push ID
    if (!wrapper.EncodeVarint(push_id_)) {
        return false;
    }

    // Write encoded fields
    if (!wrapper.EncodeBytes(encoded_fields_.data(), encoded_fields_.size())) {
        return false;
    }

    return true;
}

bool PushPromiseFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);
    
    if (with_type) {
        if (!wrapper.DecodeFixedUint16(type_)) {
            return false;
        }
    }

    // Read length
    if (!wrapper.DecodeVarint(length_)) {
        return false;
    }

    // Read push ID
    if (!wrapper.DecodeVarint(push_id_)) {
        return false;
    }

    // Read encoded fields
    uint32_t remaining = length_ - common::GetEncodeVarintLength(push_id_);
    encoded_fields_.resize(remaining);
    uint8_t* ptr = encoded_fields_.data();
    if (!wrapper.DecodeBytes(ptr, remaining)) {
        return false;
    }

    return true;
}

uint32_t PushPromiseFrame::EvaluateEncodeSize() {
    uint32_t size = 0;
    
    // Size for frame type
    size += sizeof(type_);
    
    // Size for length field
    size += common::GetEncodeVarintLength(EvaluatePayloadSize());
    
    // Size for push ID
    size += common::GetEncodeVarintLength(push_id_);

    // Size for encoded fields
    size += encoded_fields_.size();

    return size;
}

uint32_t PushPromiseFrame::EvaluatePayloadSize() {
    if (length_ == 0) {
        length_ = common::GetEncodeVarintLength(push_id_) + encoded_fields_.size();
    }
    return length_;
}

}
}
