#include "common/decode/decode.h"
#include "http3/frame/headers_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace http3 {

bool HeadersFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    if (buffer->GetFreeLength() < EvaluateEncodeSize()) {
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    // Write frame type
    if (!wrapper.EncodeFixedUint16(type_)) {
        return false;
    }

    // Write length
    if (!wrapper.EncodeVarint(length_)) {
        return false;
    }

    // Write encoded fields
    if (!wrapper.EncodeBytes(encoded_fields_.data(), encoded_fields_.size())) {
        return false;
    }

    return true;
}

bool HeadersFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
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

    // Read encoded fields
    encoded_fields_.resize(length_);
    uint8_t* ptr = encoded_fields_.data();
    if (!wrapper.DecodeBytes(ptr, length_)) {
        return false;
    }

    return true;
}

uint32_t HeadersFrame::EvaluateEncodeSize() {
    uint32_t size = 0;
    
    // Size for frame type
    size += sizeof(type_);
    
    // Size for length field
    size += common::GetEncodeVarintLength(length_);
    
    // Size for encoded fields
    size += EvaluatePaloadSize();

    return size;
}

uint32_t HeadersFrame::EvaluatePaloadSize() {
    if (length_ == 0) {
        return encoded_fields_.size();
    }
    return length_;
}

}
}
