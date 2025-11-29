#include "common/log/log.h"
#include "quic/frame/max_stream_data_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace quic {

MaxStreamDataFrame::MaxStreamDataFrame():
    IStreamFrame(FrameType::kMaxStreamData),
    maximum_data_(0) {}

MaxStreamDataFrame::~MaxStreamDataFrame() {}

bool MaxStreamDataFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
    uint16_t need_size = EncodeSize();
    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR(
            "insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    CHECK_ENCODE_ERROR(wrapper.EncodeFixedUint16(frame_type_), "failed to encode frame type");
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(stream_id_), "failed to encode stream id");
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(maximum_data_), "failed to encode maximum data");

    return true;
}

bool MaxStreamDataFrame::Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);

    if (with_type) {
        CHECK_DECODE_ERROR(wrapper.DecodeFixedUint16(frame_type_), "failed to decode frame type");
        if (frame_type_ != FrameType::kMaxStreamData) {
            common::LOG_ERROR("invalid frame type. frame_type:%d", frame_type_);
            return false;
        }
    }
    CHECK_DECODE_ERROR(wrapper.DecodeVarint(stream_id_), "failed to decode stream id");
    CHECK_DECODE_ERROR(wrapper.DecodeVarint(maximum_data_), "failed to decode maximum data");

    return true;
}

uint32_t MaxStreamDataFrame::EncodeSize() {
    // frame_type (2 bytes) + stream_id (varint) + maximum_data (varint)
    return 2 + common::GetEncodeVarintLength(stream_id_) + common::GetEncodeVarintLength(maximum_data_);
}

}  // namespace quic
}  // namespace quicx