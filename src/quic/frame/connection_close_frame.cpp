#include "common/log/log.h"
#include "connection_close_frame.h"
#include "common/decode/normal_decode.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"

namespace quicx {

ConnectionCloseFrame::ConnectionCloseFrame():
    IFrame(FT_CONNECTION_CLOSE),
    _is_application_error(false),
    _error_code(0),
    _err_frame_type(0) {

}

ConnectionCloseFrame::ConnectionCloseFrame(uint16_t frame_type):
    IFrame(frame_type),
     _is_application_error(false),
    _error_code(0),
    _err_frame_type(0) {

}

ConnectionCloseFrame::~ConnectionCloseFrame() {

}

bool ConnectionCloseFrame::Encode(std::shared_ptr<IBufferWriteOnly> buffer) {
    uint16_t need_size = EncodeSize();

    auto pos_pair = buffer->GetWritePair();
    auto remain_size = pos_pair.second - pos_pair.first;
    if (need_size > remain_size) {
        LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }

    char* pos = pos_pair.first;
    pos = EncodeFixed<uint16_t>(pos, _frame_type);
    pos = EncodeVarint(pos, _error_code);
    pos = EncodeVarint(pos, _err_frame_type);
    pos = EncodeVarint(pos, _reason.length());

    buffer->MoveWritePt(pos - pos_pair.first);
    buffer->Write(_reason.c_str(), _reason.length());
    return true;
}

bool ConnectionCloseFrame::Decode(std::shared_ptr<IBufferReadOnly> buffer, bool with_type) {
    uint16_t size = EncodeSize();

    auto pos_pair = buffer->GetReadPair();
    char* pos = pos_pair.first;

    if (with_type) {
        pos = DecodeFixed<uint16_t>(pos, pos_pair.second, _frame_type);
        if (_frame_type != FT_CONNECTION_CLOSE) {
            return false;
        }
    }

    uint32_t reason_length = 0;
    pos = DecodeVarint(pos, pos_pair.second, _error_code);
    pos = DecodeVarint(pos, pos_pair.second, _err_frame_type);
    pos = DecodeVarint(pos, pos_pair.second, reason_length);
    
    buffer->MoveReadPt(pos - pos_pair.first);

    auto pos_pair = buffer->GetReadPair();
    auto remain_size = pos_pair.second - pos_pair.first;
    if (reason_length > remain_size) {
        return false;
    }
    
    _reason.clear();
    _reason.append(pos_pair.first, reason_length);
    buffer->MoveReadPt(reason_length);
    return true;
}

uint32_t ConnectionCloseFrame::EncodeSize() {
    return sizeof(ConnectionCloseFrame);
}

}