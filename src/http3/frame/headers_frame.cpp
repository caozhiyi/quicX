#include "common/decode/decode.h"
#include "http3/frame/headers_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace http3 {

bool HeadersFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
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
    wrapper.Flush();

    // Don't write encoded fields, process it outside for no copy
    buffer->Write(encoded_fields_);
    return true;
}

bool HeadersFrame::Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type) {
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
    wrapper.Flush();
    
    // Check if we have enough data
    if (buffer->GetDataLength() < length_) {
        return false;
    }
    
    // Read encoded fields - only the specified length
    encoded_fields_ = buffer->ShallowClone();
    if (encoded_fields_->GetDataLength() > length_) {
        encoded_fields_->MoveWritePt(-(static_cast<int32_t>(encoded_fields_->GetDataLength() - length_)));
    }
    
    // Advance the buffer read pointer
    buffer->MoveReadPt(length_);

    return true;
}

uint32_t HeadersFrame::EvaluateEncodeSize() {
    uint32_t size = 0;
    
    // Size for frame type
    size += sizeof(type_);
    
    // Size for length field
    size += common::GetEncodeVarintLength(length_);
    
    // Size for encoded fields
    size += EvaluatePayloadSize();

    return size;
}

uint32_t HeadersFrame::EvaluatePayloadSize() {
    if (length_ == 0) {
        length_ = encoded_fields_->GetDataLength();
    }
    return length_;
}

}
}
