#include "common/log/log.h"
#include "common/decode/decode.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"
#include "quic/frame/connection_close_frame.h"

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

    auto span = buffer->GetWriteSpan();
    auto remain_size = span.GetLength();
    if (need_size > remain_size) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }

    uint8_t* pos = span.GetStart();
    pos = common::FixedEncodeUint16(pos, frame_type_);
    pos = common::EncodeVarint(pos, error_code_);
    pos = common::EncodeVarint(pos, err_frame_type_);
    pos = common::EncodeVarint(pos, reason_.length());

    buffer->MoveWritePt(pos - span.GetStart());
    buffer->Write((uint8_t*)reason_.data(), reason_.length());
    return true;
}

bool ConnectionCloseFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    uint16_t size = EncodeSize();

    auto span = buffer->GetReadSpan();
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    if (with_type) {
        pos = common::FixedDecodeUint16(pos, end, frame_type_);
        if (frame_type_ != FT_CONNECTION_CLOSE) {
            return false;
        }
    }

    uint32_t reason_length = 0;
    pos = common::DecodeVarint(pos, end, error_code_);
    pos = common::DecodeVarint(pos, end, err_frame_type_);
    pos = common::DecodeVarint(pos, end, reason_length);
    
    buffer->MoveReadPt(pos - span.GetStart());

    span = buffer->GetReadSpan();
    auto remain_size = span.GetLength();
    if (reason_length > remain_size) {
        return false;
    }
    
    reason_.clear();
    reason_.append((const char*)span.GetStart(), reason_length);
    buffer->MoveReadPt(reason_length);
    return true;
}

uint32_t ConnectionCloseFrame::EncodeSize() {
    return sizeof(ConnectionCloseFrame);
}

}
}