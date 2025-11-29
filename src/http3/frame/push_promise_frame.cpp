#include "http3/frame/push_promise_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/multi_block_buffer_decode_wrapper.h"

namespace quicx {
namespace http3 {

bool PushPromiseFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
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
    wrapper.Flush();

    buffer->Write(encoded_fields_);

    return true;
}

DecodeResult PushPromiseFrame::Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type) {
    common::MultiBlockBufferDecodeWrapper wrapper(buffer);

    if (with_type) {
        if (!wrapper.DecodeFixedUint16(type_)) {
            return DecodeResult::kError;
        }
    }

    // Read length
    if (!wrapper.DecodeVarint(length_)) {
        return DecodeResult::kError;
    }

    // Check if we have enough data
    if (wrapper.GetDataLength() < length_) {
        return DecodeResult::kError;
    }

    // Read push ID
    if (!wrapper.DecodeVarint(push_id_)) {
        return DecodeResult::kError;
    }
    wrapper.Flush();

    // Calculate remaining length for encoded fields
    uint32_t push_id_size = common::GetEncodeVarintLength(push_id_);
    uint32_t fields_length = length_ - push_id_size;

    // Check if we have enough data for fields
    if (wrapper.GetBuffer()->GetDataLength() < fields_length) {
        return DecodeResult::kNeedMoreData;
    }

    // Read encoded fields - only the remaining length
    encoded_fields_ = wrapper.GetBuffer()->CloneReadable(fields_length);

    return DecodeResult::kSuccess;
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
    size += encoded_fields_->GetDataLength();

    return size;
}

uint32_t PushPromiseFrame::EvaluatePayloadSize() {
    if (length_ == 0) {
        length_ = common::GetEncodeVarintLength(push_id_) + encoded_fields_->GetDataLength();
    }
    return length_;
}

}  // namespace http3
}  // namespace quicx
