#include "common/log/log.h"
#include "quic/frame/stop_sending_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace quic {

StopSendingFrame::StopSendingFrame(): 
    IStreamFrame(FT_STOP_SENDING),
    app_error_code_(0) {

}

StopSendingFrame::~StopSendingFrame() {

}

bool StopSendingFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    uint16_t need_size = EncodeSize();
    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    wrapper.EncodeFixedUint16(frame_type_);
    wrapper.EncodeVarint(stream_id_);
    wrapper.EncodeVarint(app_error_code_);
    return true;
}

bool StopSendingFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);

    if (with_type) {
        wrapper.DecodeFixedUint16(frame_type_);
        if (frame_type_ != FT_STOP_SENDING) {
            return false;
        }
    }
    wrapper.DecodeVarint(stream_id_);
    wrapper.DecodeVarint(app_error_code_);
    return true;
}

uint32_t StopSendingFrame::EncodeSize() {
    return sizeof(StopSendingFrame);
}

}
}