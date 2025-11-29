#include "common/log/log.h"
#include "quic/frame/new_token_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace quic {

NewTokenFrame::NewTokenFrame():
    IFrame(FrameType::kNewToken) {}

NewTokenFrame::~NewTokenFrame() {}

bool NewTokenFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
    uint16_t need_size = EncodeSize();
    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR(
            "insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    CHECK_ENCODE_ERROR(wrapper.EncodeFixedUint16(frame_type_), "failed to encode frame type");
    CHECK_ENCODE_ERROR(wrapper.EncodeVarint(token_length_), "failed to encode token length");
    CHECK_ENCODE_ERROR(wrapper.EncodeBytes(token_, token_length_), "failed to encode token");
    return true;
}

bool NewTokenFrame::Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);

    if (with_type) {
        CHECK_DECODE_ERROR(wrapper.DecodeFixedUint16(frame_type_), "failed to decode frame type");
        if (frame_type_ != FrameType::kNewToken) {
            common::LOG_ERROR("invalid frame type. frame_type:%d", frame_type_);
            return false;
        }
    }
    CHECK_DECODE_ERROR(wrapper.DecodeVarint(token_length_), "failed to decode token length");
    wrapper.Flush();
    if (token_length_ > buffer->GetDataLength()) {
        common::LOG_ERROR(
            "insufficient remaining data. remain_size:%d, need_size:%d", buffer->GetDataLength(), token_length_);
        return false;
    }

    CHECK_DECODE_ERROR(wrapper.DecodeBytes(token_, token_length_, false), "failed to decode token");
    return true;
}

uint32_t NewTokenFrame::EncodeSize() {
    return sizeof(NewTokenFrame);
}

}  // namespace quic
}  // namespace quicx