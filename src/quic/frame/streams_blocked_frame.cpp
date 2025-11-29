#include "common/log/log.h"
#include "quic/frame/streams_blocked_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"
namespace quicx {
namespace quic {

StreamsBlockedFrame::StreamsBlockedFrame(uint16_t frame_type):
    IFrame(frame_type),
    maximum_streams_(0) {}

StreamsBlockedFrame::~StreamsBlockedFrame() {}

bool StreamsBlockedFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
    uint16_t need_size = EncodeSize();
    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR(
            "insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    CHECK_ENCODE_ERROR(wrapper.EncodeFixedUint16(frame_type_), "failed to encode frame type");
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(maximum_streams_), "failed to encode maximum streams");
    return true;
}

bool StreamsBlockedFrame::Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);

    if (with_type) {
        CHECK_DECODE_ERROR(wrapper.DecodeFixedUint16(frame_type_), "failed to decode frame type");
        if (frame_type_ != FrameType::kStreamsBlockedBidirectional &&
            frame_type_ != FrameType::kStreamsBlockedUnidirectional) {
            common::LOG_ERROR("invalid frame type. frame_type:%d", frame_type_);
            return false;
        }
    }
    CHECK_DECODE_ERROR(wrapper.DecodeVarint(maximum_streams_), "failed to decode maximum streams");
    return true;
}

uint32_t StreamsBlockedFrame::EncodeSize() {
    return sizeof(StreamsBlockedFrame);
}

}  // namespace quic
}  // namespace quicx
