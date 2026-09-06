#include "common/buffer/buffer_decode_wrapper.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/log/log.h"

#include "http3/config.h"
#include "http3/frame/push_promise_frame.h"

namespace quicx {
namespace http3 {

bool PushPromiseFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
    if (buffer->GetFreeLength() < EvaluateEncodeSize()) {
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    // Write frame type (varint per RFC 9114)
    if (!wrapper.EncodeVarint(type_)) {
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
    common::BufferDecodeWrapper wrapper(buffer);

    if (with_type) {
        uint64_t frame_type;
        if (!wrapper.DecodeVarint(frame_type)) {
            return DecodeResult::kError;
        }
        type_ = static_cast<uint16_t>(frame_type);
    }

    // Read length
    if (!wrapper.DecodeVarint(length_)) {
        return DecodeResult::kError;
    }

    if (length_ > kMaxFrameLength) {
        LOG_ERROR("PushPromiseFrame::Decode: length %llu exceeds limit %llu", (unsigned long long)length_,
            (unsigned long long)kMaxFrameLength);
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
    // The Push ID is part of the frame payload, so a Length smaller than the Push
    // ID's own encoding is malformed. Without this guard the subtraction wraps and
    // fields_length becomes a huge value.
    if (length_ < push_id_size) {
        LOG_ERROR("PushPromiseFrame::Decode: length %llu smaller than push id size %u", (unsigned long long)length_,
            push_id_size);
        return DecodeResult::kError;
    }
    uint32_t fields_length = static_cast<uint32_t>(length_) - push_id_size;

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

    // Size for frame type (varint per RFC 9114)
    size += common::GetEncodeVarintLength(type_);

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
