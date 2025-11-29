#include "common/log/log.h"
#include "quic/frame/padding_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace quic {

PaddingFrame::PaddingFrame():
    IFrame(FrameType::kPadding),
    padding_length_(0) {

}

PaddingFrame::~PaddingFrame() {

}

bool PaddingFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
    uint16_t need_size = EncodeSize();
    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    CHECK_ENCODE_ERROR(wrapper.EncodeFixedUint16(frame_type_), "failed to encode frame type");
    for (size_t i = 0; i < padding_length_; i++) {
        CHECK_ENCODE_ERROR(wrapper.EncodeFixedUint8(0x00), "failed to encode padding");
    }
    return true;
}

bool PaddingFrame::Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);

    if (with_type) {
        CHECK_DECODE_ERROR(wrapper.DecodeFixedUint16(frame_type_), "failed to decode frame type");
        if (frame_type_ != FrameType::kPadding) {
            common::LOG_ERROR("invalid frame type. frame_type:%d", frame_type_);
            return false;
        }
    }
    wrapper.Flush();

    uint8_t byte;
    auto span = buffer->GetReadableSpan();
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();
    while (pos < end) {
        byte = *pos;
        if (byte != 0x00) {
            break;
        }
        pos++;
        padding_length_++;
    }
    buffer->MoveReadPt(pos - span.GetStart());
    return true;
}

uint32_t PaddingFrame::EncodeSize() {
    return sizeof(PaddingFrame) + padding_length_;
}

}
}