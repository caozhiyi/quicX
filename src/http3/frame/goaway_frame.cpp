#include "http3/frame/goaway_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/multi_block_buffer_decode_wrapper.h"

namespace quicx {
namespace http3 {

bool GoAwayFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
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

    // Write stream ID
    if (!wrapper.EncodeVarint(stream_id_)) {
        return false;
    }

    return true;
}

DecodeResult GoAwayFrame::Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type) {
    common::MultiBlockBufferDecodeWrapper wrapper(buffer);

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

    // TODO: check length
    // Read stream ID
    if (!wrapper.DecodeVarint(stream_id_)) {
        return DecodeResult::kError;
    }

    return DecodeResult::kSuccess;
}

uint32_t GoAwayFrame::EvaluateEncodeSize() {
    uint32_t size = 0;

    // Size for frame type (varint per RFC 9114)
    size += common::GetEncodeVarintLength(type_);

    // Size for length field
    size += common::GetEncodeVarintLength(EvaluatePayloadSize());

    // Size for stream ID
    size += common::GetEncodeVarintLength(stream_id_);

    return size;
}

uint32_t GoAwayFrame::EvaluatePayloadSize() {
    return common::GetEncodeVarintLength(stream_id_);
}

}  // namespace http3
}  // namespace quicx
