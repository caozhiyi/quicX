#include "common/log/log.h"
#include "quic/frame/path_response_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace quic {


PathResponseFrame::PathResponseFrame(): 
    IFrame(FT_PATH_RESPONSE) {
    memset(data_, 0, __path_data_length);
}

PathResponseFrame::~PathResponseFrame() {

}

bool PathResponseFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    uint16_t need_size = EncodeSize();
    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    wrapper.EncodeFixedUint16(frame_type_);
    wrapper.EncodeBytes(data_, __path_data_length);
    return true;
}

bool PathResponseFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);

    if (with_type) {
        wrapper.DecodeFixedUint16(frame_type_);
        if (frame_type_ != FT_PATH_RESPONSE) {
            return false;
        } 
    }

    wrapper.Flush();
    if (__path_data_length > buffer->GetDataLength()) {
        common::LOG_ERROR("insufficient remaining data. remain_size:%d, need_size:%d", buffer->GetDataLength(), __path_data_length);
        return false;
    }
    auto data = (uint8_t*)data_;
    wrapper.DecodeBytes(data, __path_data_length);
    return true;
}

uint32_t PathResponseFrame::EncodeSize() {
    return sizeof(PathResponseFrame);
}

void PathResponseFrame::SetData(uint8_t* data) {
    memcpy(data_, data, __path_data_length);
}

}
}