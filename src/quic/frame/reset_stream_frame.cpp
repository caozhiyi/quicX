#include "common/log/log.h"
#include "quic/frame/reset_stream_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace quic {

ResetStreamFrame::ResetStreamFrame(): 
    IStreamFrame(FT_RESET_STREAM),
    app_error_code_(0), 
    final_size_(0) {

}

ResetStreamFrame::~ResetStreamFrame() {

}

bool ResetStreamFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    uint16_t need_size = EncodeSize();
    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    wrapper.EncodeFixedUint16(frame_type_);
    wrapper.EncodeVarint(stream_id_);
    wrapper.EncodeVarint(app_error_code_);
    wrapper.EncodeVarint(final_size_);

    return true;
}

bool ResetStreamFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);

    if (with_type) {
        wrapper.DecodeFixedUint16(frame_type_);
        if (frame_type_ != FT_RESET_STREAM) {
            return false;
        }
    }
    wrapper.DecodeVarint(stream_id_);
    wrapper.DecodeVarint(app_error_code_);
    wrapper.DecodeVarint(final_size_);
    return true;
}

uint32_t ResetStreamFrame::EncodeSize() {
    return sizeof(ResetStreamFrame);
}

}
}