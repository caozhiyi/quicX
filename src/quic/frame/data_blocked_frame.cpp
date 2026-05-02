#include "data_blocked_frame.h"
#include "common/buffer/buffer_decode_wrapper.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/log/log.h"

namespace quicx {
namespace quic {

DataBlockedFrame::DataBlockedFrame():
    IFrame(FrameType::kDataBlocked),
    maximum_data_(0) {}

DataBlockedFrame::~DataBlockedFrame() {}

bool DataBlockedFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
    uint32_t need_size = EncodeSize();

    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR(
            "insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(frame_type_), "failed to encode frame type");
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(maximum_data_), "failed to encode maximum data");
    return true;
}

bool DataBlockedFrame::Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);

    if (with_type) {
        uint64_t type = 0;
        CHECK_DECODE_ERROR(wrapper.DecodeVarint(type), "failed to decode frame type");
        frame_type_ = static_cast<uint16_t>(type);
        if (frame_type_ != FrameType::kDataBlocked) {
            common::LOG_ERROR("invalid frame type. frame_type:%d", frame_type_);
            return false;
        }
    }
    CHECK_DECODE_ERROR(wrapper.DecodeVarint(maximum_data_), "failed to decode maximum data");
    return true;
}

uint32_t DataBlockedFrame::EncodeSize() {
    return common::GetEncodeVarintLength(frame_type_) + common::GetEncodeVarintLength(maximum_data_);
}

}  // namespace quic
}  // namespace quicx
