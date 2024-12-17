#include "common/log/log.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"
#include "quic/frame/retire_connection_id_frame.h"

namespace quicx {
namespace quic {

RetireConnectionIDFrame::RetireConnectionIDFrame():
    IFrame(FT_RETIRE_CONNECTION_ID),
    sequence_number_(0) {

}

RetireConnectionIDFrame::~RetireConnectionIDFrame() {

}

bool RetireConnectionIDFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    uint16_t need_size = EncodeSize();
    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    wrapper.EncodeFixedUint16(frame_type_);
    wrapper.EncodeVarint(sequence_number_);
    return true;
}

bool RetireConnectionIDFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);

    if (with_type) {
        wrapper.DecodeFixedUint16(frame_type_);
        if (frame_type_ != FT_RETIRE_CONNECTION_ID) {
            return false;
        }
    }
    wrapper.DecodeVarint(sequence_number_);
    return true;
}

uint32_t RetireConnectionIDFrame::EncodeSize() {
    return sizeof(RetireConnectionIDFrame);
}
  
}
}