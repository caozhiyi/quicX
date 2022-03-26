#include "common/log/log.h"
#include "quic/frame/new_token_frame.h"
#include "common/decode/normal_decode.h"


namespace quicx {

NewTokenFrame::NewTokenFrame():
    IFrame(FT_NEW_TOKEN) {

}

NewTokenFrame::~NewTokenFrame() {

}

bool NewTokenFrame::Encode(std::shared_ptr<IBufferWriteOnly> buffer) {
    uint16_t need_size = EncodeSize();
    auto pos_pair = buffer->GetWritePair();
    auto remain_size = pos_pair.second - pos_pair.first;
    if (need_size > remain_size) {
        LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }

    char* pos = pos_pair.first;
    pos = EncodeFixed<uint16_t>(pos, _frame_type);
    pos = EncodeVarint(pos, _token_length);

    buffer->MoveWritePt(pos - pos_pair.first);
    buffer->Write(_token, _token_length);
    return true;
}

bool NewTokenFrame::Decode(std::shared_ptr<IBufferReadOnly> buffer, bool with_type) {
    auto pos_pair = buffer->GetReadPair();
    char* pos = pos_pair.first;

    if (with_type) {
        pos = DecodeFixed<uint16_t>(pos, pos_pair.second, _frame_type);
        if (_frame_type != FT_NEW_TOKEN) {
            return false;
        }
    }
    pos = DecodeVarint(pos, pos_pair.second, _token_length);

    buffer->MoveReadPt(pos - pos_pair.first);
    if (_token_length > buffer->GetCanReadLength()) {
        LOG_ERROR("insufficient remaining data. remain_size:%d, need_size:%d", buffer->GetCanReadLength(), _token_length);
        return false;
    }
    
    _token = pos;
    buffer->MoveReadPt(_token_length);
    return true;
}

uint32_t NewTokenFrame::EncodeSize() {
    return sizeof(NewTokenFrame);
}

}