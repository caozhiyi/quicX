#include "common/buffer/buffer_decode_wrapper.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/log/log.h"

#include "quic/frame/stream_frame.h"

namespace quicx {
namespace quic {

StreamFrame::StreamFrame():
    IStreamFrame(FrameType::kStream),
    offset_(0),
    length_(0) {}

StreamFrame::StreamFrame(uint16_t frame_type):
    IStreamFrame(frame_type),
    offset_(0) {}

StreamFrame::~StreamFrame() {}

bool StreamFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
    uint16_t need_size = EncodeSize();
    if (need_size > buffer->GetFreeLength()) {
        common::LOG_DEBUG(
            "insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }

    // Set length flag when encoding (QUIC typically includes length for proper frame parsing)
    if (length_ > 0) {
        frame_type_ |= kLenFlag;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    CHECK_ENCODE_ERROR(wrapper.EncodeFixedUint16(frame_type_), "failed to encode frame type");
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(stream_id_), "failed to encode stream id");
    if (HasOffset()) {
        CHECK_ENCODE_ERROR(wrapper.EncodeVarint(offset_), "failed to encode offset");
    }
    if (HasLength()) {
        CHECK_ENCODE_ERROR(wrapper.EncodeVarint(length_), "failed to encode length");
    }
    CHECK_ENCODE_ERROR(wrapper.EncodeBytes(data_.GetStart(), length_), "failed to encode data");
    return true;
}

bool StreamFrame::Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type) {
    uint32_t initial_buffer_len = buffer->GetDataLength();

    common::BufferDecodeWrapper wrapper(buffer);

    if (with_type) {
        CHECK_DECODE_ERROR(wrapper.DecodeFixedUint16(frame_type_), "failed to decode frame type");
    }
    CHECK_DECODE_ERROR(wrapper.DecodeVarint(stream_id_), "failed to decode stream id");
    if (HasOffset()) {
        CHECK_DECODE_ERROR(wrapper.DecodeVarint(offset_), "failed to decode offset");
    }
    if (HasLength()) {
        CHECK_DECODE_ERROR(wrapper.DecodeVarint(length_), "failed to decode length");
    }

    // Flush first to advance the buffer's read pointer
    wrapper.Flush();

    // If no length field, stream data extends to the end of the buffer (after Flush!)
    if (!HasLength()) {
        length_ = buffer->GetDataLength();
    }

    if (length_ > buffer->GetDataLength()) {
        return false;
    }
    data_ = buffer->GetSharedReadableSpan(length_);
    buffer->MoveReadPt(length_);

    return true;
}

uint32_t StreamFrame::EncodeSize() {
    // frame type encoded as fixed uint16
    uint32_t size = sizeof(uint16_t);
    // Stream ID (always present)
    size += common::GetEncodeVarintLength(stream_id_);
    // Offset (if present) - check actual value, not flag, since flag may not be set yet
    if (HasOffset() || offset_ > 0) {
        size += common::GetEncodeVarintLength(offset_);
    }
    // Length (if present) - check actual value, not flag, since flag may not be set yet
    // RFC 9000: Length field is included if length > 0
    if (HasLength() || length_ > 0) {
        size += common::GetEncodeVarintLength(length_);
    }
    // Data payload
    size += length_;
    return size;
}

void StreamFrame::SetOffset(uint64_t offset) {
    offset_ = offset;
    frame_type_ |= kOffFlag;
}

bool StreamFrame::IsStreamFrame(uint16_t frame_type) {
    return (frame_type & ~kMaskFlag) == FrameType::kStream;
}

}  // namespace quic
}  // namespace quicx