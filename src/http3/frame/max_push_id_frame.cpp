#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

#include "http3/frame/max_push_id_frame.h"


namespace quicx {
namespace http3 {

bool MaxPushIdFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
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

    return true;
}

DecodeResult MaxPushIdFrame::Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);

    if (with_type) {
        uint64_t frame_type;
        if (!wrapper.DecodeVarint(frame_type)) {
            return DecodeResult::kError;
        }
        type_ = static_cast<uint16_t>(frame_type);
    }

    uint64_t length;
    // Read length
    if (!wrapper.DecodeVarint(length)) {
        return DecodeResult::kError;
    }

    // Read push ID
    if (!wrapper.DecodeVarint(push_id_)) {
        return DecodeResult::kError;
    }

    return DecodeResult::kSuccess;
}

uint32_t MaxPushIdFrame::EvaluateEncodeSize() {
    uint32_t size = 0;

    // Size for frame type (varint per RFC 9114)
    size += common::GetEncodeVarintLength(type_);

    // Size for length field
    size += common::GetEncodeVarintLength(EvaluatePayloadSize());

    // Size for push ID
    size += common::GetEncodeVarintLength(push_id_);

    return size;
}

uint32_t MaxPushIdFrame::EvaluatePayloadSize() {
    return common::GetEncodeVarintLength(push_id_);
}

}  // namespace http3
}  // namespace quicx
