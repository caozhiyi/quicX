#include "common/log/log.h"
#include "quic/frame/max_stream_data_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace quic {


MaxStreamDataFrame::MaxStreamDataFrame():
    IStreamFrame(FrameType::kMaxStreamData),
    maximum_data_(0) {

}

MaxStreamDataFrame::~MaxStreamDataFrame() {

}

bool MaxStreamDataFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    uint16_t need_size = EncodeSize();
    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    wrapper.EncodeFixedUint16(frame_type_);
    wrapper.EncodeVarint(stream_id_);
    wrapper.EncodeVarint(maximum_data_);

    return true;
}

bool MaxStreamDataFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);

    if (with_type) {
        wrapper.DecodeFixedUint16(frame_type_);
        if (frame_type_ != FrameType::kMaxStreamData) {
            common::LOG_ERROR("invalid frame type. frame_type:%d", frame_type_);
            return false;
        }
    }
    wrapper.DecodeVarint(stream_id_);
    wrapper.DecodeVarint(maximum_data_);

    return true;
}

uint32_t MaxStreamDataFrame::EncodeSize() {
    return sizeof(MaxStreamDataFrame);
}

}
}