#include "common/log/log.h"
#include "data_blocked_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace quic {

DataBlockedFrame::DataBlockedFrame():
    IFrame(FT_DATA_BLOCKED),
    maximum_data_(0) {

}

DataBlockedFrame::~DataBlockedFrame() {

}

bool DataBlockedFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    uint16_t need_size = EncodeSize();

    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    wrapper.EncodeFixedUint16(frame_type_);
    wrapper.EncodeVarint(maximum_data_);
    return true;
}

bool DataBlockedFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);

    if (with_type) {
        wrapper.DecodeFixedUint16(frame_type_);
        if (frame_type_ != FT_DATA_BLOCKED) {
            return false;
        }
    }
    wrapper.DecodeVarint(maximum_data_);
    return true;
}

uint32_t DataBlockedFrame::EncodeSize() {
    return sizeof(DataBlockedFrame);
}

}
}
