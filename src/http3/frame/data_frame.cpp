#include "http3/frame/data_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace http3 {

bool DataFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
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
    wrapper.Flush();
    buffer->Write(data_);
    return true;
}

bool DataFrame::Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type) {
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

    wrapper.Flush();
    
    // Check if we have enough data
    if (buffer->GetDataLength() < length_) {
        return false;
    }
    
    // Get only the specified length of data
    data_ = buffer->ShallowClone();
    if (data_->GetDataLength() > length_) {
        data_->MoveWritePt(-(static_cast<int32_t>(data_->GetDataLength() - length_)));
    }
    
    // Advance the buffer read pointer
    buffer->MoveReadPt(length_);

    return true;
}

uint32_t DataFrame::EvaluateEncodeSize() {
    uint32_t size = 0;
    
    // Frame type size
    size += sizeof(type_);;
    
    // Data size (this also updates length_ if it's 0)
    size += EvaluatePayloadSize();
    
    // Length field size (must be after EvaluatePayloadSize)
    size += common::GetEncodeVarintLength(length_);
    
    return size;
}

uint32_t DataFrame::EvaluatePayloadSize() {
    if (length_ == 0) { 
        length_ = data_->GetDataLength();
    }
    return length_;
}

}
}
