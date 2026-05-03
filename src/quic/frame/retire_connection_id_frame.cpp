#include "quic/frame/retire_connection_id_frame.h"
#include "common/buffer/buffer_decode_wrapper.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/log/log.h"

namespace quicx {
namespace quic {

RetireConnectionIDFrame::RetireConnectionIDFrame():
    IFrame(FrameType::kRetireConnectionId),
    sequence_number_(0) {}

RetireConnectionIDFrame::~RetireConnectionIDFrame() {}

bool RetireConnectionIDFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
    uint32_t need_size = EncodeSize();
    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR(
            "insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(frame_type_), "failed to encode frame type");
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(sequence_number_), "failed to encode sequence number");
    return true;
}

bool RetireConnectionIDFrame::Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);

    if (with_type) {
        uint64_t type = 0;
        CHECK_DECODE_ERROR(wrapper.DecodeVarint(type), "failed to decode frame type");
        frame_type_ = static_cast<uint16_t>(type);
        if (frame_type_ != FrameType::kRetireConnectionId) {
            common::LOG_ERROR("invalid frame type. frame_type:%d", frame_type_);
            return false;
        }
    }
    CHECK_DECODE_ERROR(wrapper.DecodeVarint(sequence_number_), "failed to decode sequence number");
    return true;
}

uint32_t RetireConnectionIDFrame::EncodeSize() {
    return common::GetEncodeVarintLength(frame_type_) + common::GetEncodeVarintLength(sequence_number_);
}

}  // namespace quic
}  // namespace quicx