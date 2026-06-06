#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

#include "http3/frame/goaway_frame.h"

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

    // RFC 9114 §7.2.6: GOAWAY payload is a single varint Stream/Push ID.
    // A varint is at most 8 bytes (RFC 9000 §16). A length of 0, or a length
    // larger than 8, cannot be a well-formed GOAWAY and MUST be rejected
    // (treat as H3_FRAME_ERROR at the caller).
    if (length == 0 || length > 8) {
        return DecodeResult::kError;
    }

    // Read stream ID and confirm the consumed bytes match the declared length
    // exactly. A mismatch means either the encoder lied about the payload
    // size or there is trailing junk; both are protocol violations and could
    // otherwise let an attacker desynchronise the frame stream.
    uint32_t before = wrapper.GetReadLength();
    if (!wrapper.DecodeVarint(stream_id_)) {
        return DecodeResult::kError;
    }
    uint32_t id_bytes = wrapper.GetReadLength() - before;
    if (id_bytes != length) {
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
