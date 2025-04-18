#include "http3/frame/cancel_push_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace http3 {

bool CancelPushFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
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

    return true;
}

bool CancelPushFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);
    
    if (with_type) {
        if (!wrapper.DecodeFixedUint16(type_)) {
            return false;
        }
    }

    // Read length
    uint64_t length = 0;
    if (!wrapper.DecodeVarint(length)) {
        return false;
    }

    // Read push ID
    if (!wrapper.DecodeVarint(push_id_)) {
        return false;
    }

    return true;
}

uint32_t CancelPushFrame::EvaluateEncodeSize() {
    uint32_t size = 0;
    
    // Size for frame type
    size += sizeof(type_);
    
    // Size for length field
    size += common::GetEncodeVarintLength(EvaluatePayloadSize());
    
    // Size for push ID
    size += common::GetEncodeVarintLength(push_id_);

    return size;
}

uint32_t CancelPushFrame::EvaluatePayloadSize() {
    return common::GetEncodeVarintLength(push_id_);
}

}
}
