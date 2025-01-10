#include "common/log/log.h"
#include "quic/frame/padding_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace quic {

bool PaddingFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    uint16_t need_size = EncodeSize();
    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    wrapper.EncodeFixedUint16(frame_type_);
    for (size_t i = 0; i < padding_length_; i++) {
        wrapper.EncodeFixedUint8(0x00);
    }
    return true;
}

bool PaddingFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);

    if (with_type) {
        wrapper.DecodeFixedUint16(frame_type_);
        if (frame_type_ != FT_PADDING) {
            return false;
        }
    }
    wrapper.Flush();

    uint8_t byte;
    auto span = buffer->GetReadSpan();
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