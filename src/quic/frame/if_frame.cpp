#include "common/buffer/buffer_decode_wrapper.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/log/log.h"

#include "quic/frame/if_frame.h"
#include "quic/frame/stream_frame.h"

namespace quicx {
namespace quic {

IFrame::IFrame(uint16_t ft):
    frame_type_(ft) {}

IFrame::~IFrame() {}

uint16_t IFrame::GetType() {
    return frame_type_;
}

bool IFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
    uint16_t need_size = EncodeSize();

    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR(
            "insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(frame_type_), "failed to encode frame type");

    return true;
}

bool IFrame::Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type) {
    if (with_type) {
        common::BufferDecodeWrapper wrapper(buffer);
        uint64_t type = 0;
        CHECK_DECODE_ERROR(wrapper.DecodeVarint(type), "failed to decode frame type");
        frame_type_ = static_cast<uint16_t>(type);
    }
    return true;
}

uint32_t IFrame::EncodeSize() {
    return common::GetEncodeVarintLength(frame_type_);
}

uint32_t IFrame::GetFrameTypeBit() {
    if (StreamFrame::IsStreamFrame(frame_type_)) {
        return FrameTypeBit::kStreamBit;
    }

    return (uint32_t)1 << frame_type_;
}

}  // namespace quic
}  // namespace quicx