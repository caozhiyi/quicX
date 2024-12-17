#include "http3/frame/data_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace http3 {

bool DataFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    if (buffer->GetFreeLength() < EvaluateEncodeSize()) {
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    
    // Encode frame type if needed
    if (!wrapper.EncodeFixedUint16(type_)) {
        return false;
    }

    // Encode length
    if (!wrapper.EncodeVarint(length_)) {
        return false;
    }

    // Encode data
    if (!wrapper.EncodeBytes(data_.data(), data_.size())) {
        return false;
    }

    return true;
}

bool DataFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);

    // Decode frame type if needed
    if (with_type) {
        if (!wrapper.DecodeFixedUint16(type_)) {
            return false;
        }   
    }

    // Decode length
    if (!wrapper.DecodeVarint(length_)) {
        return false;
    }

    // Decode data
    data_.resize(length_);
    uint8_t* ptr = data_.data();
    if (!wrapper.DecodeBytes(ptr, length_)) {
        return false;
    }

    return true;
}

uint32_t DataFrame::EvaluateEncodeSize() {
    uint32_t size = 0;
    
    // Frame type size
    size += sizeof(type_);;
    
    // Length field size
    size += common::GetEncodeVarintLength(length_);
    
    // Data size
    size += EvaluatePaloadSize();
    
    return size;
}

uint32_t DataFrame::EvaluatePaloadSize() {
    if (length_ == 0) { 
        length_ = data_.size();
    }
    return length_;
}

}
}
