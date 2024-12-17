#include "common/log/log.h"
#include "quic/frame/connection_close_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace quic {

ConnectionCloseFrame::ConnectionCloseFrame():
    IFrame(FT_CONNECTION_CLOSE),
    is_application_error_(false),
    error_code_(0),
    err_frame_type_(0) {

}

ConnectionCloseFrame::ConnectionCloseFrame(uint16_t frame_type):
    IFrame(frame_type),
     is_application_error_(false),
    error_code_(0),
    err_frame_type_(0) {

}

ConnectionCloseFrame::~ConnectionCloseFrame() {

}

bool ConnectionCloseFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    uint16_t need_size = EncodeSize();

    if (need_size > buffer->GetFreeLength()) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", buffer->GetFreeLength(), need_size);
        return false;
    }

    common::BufferEncodeWrapper wrapper(buffer);
    wrapper.EncodeFixedUint16(frame_type_);
    wrapper.EncodeVarint(error_code_);
    wrapper.EncodeVarint(err_frame_type_);
    wrapper.EncodeVarint(reason_.length());

    wrapper.EncodeBytes((uint8_t*)reason_.data(), reason_.length());
    return true;
}

bool ConnectionCloseFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    uint16_t size = EncodeSize();

    
    common::BufferDecodeWrapper wrapper(buffer);
    if (with_type) {
        wrapper.DecodeFixedUint16(frame_type_);
        if (frame_type_ != FT_CONNECTION_CLOSE) {
            return false;
        }
    }

    uint32_t reason_length = 0;
    wrapper.DecodeVarint(error_code_);
    wrapper.DecodeVarint(err_frame_type_);
    wrapper.DecodeVarint(reason_length);
    wrapper.Flush();
    
    if (reason_length > buffer->GetDataLength()) {
        return false;
    }
    
    reason_.resize(reason_length);
    auto data = (uint8_t*)reason_.data();
    wrapper.DecodeBytes(data, reason_length);
    return true;
}

uint32_t ConnectionCloseFrame::EncodeSize() {
    return sizeof(ConnectionCloseFrame);
}

}
}