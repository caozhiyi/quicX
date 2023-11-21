#include "common/log/log.h"
#include "common/decode/decode.h"
#include "quic/frame/new_token_frame.h"


namespace quicx {
namespace quic {

NewTokenFrame::NewTokenFrame():
    IFrame(FT_NEW_TOKEN) {

}

NewTokenFrame::~NewTokenFrame() {

}

bool NewTokenFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    uint16_t need_size = EncodeSize();
    auto span = buffer->GetWriteSpan();
    auto remain_size = span.GetLength();
    if (need_size > remain_size) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }

    uint8_t* pos = span.GetStart();
    pos = common::FixedEncodeUint16(pos, _frame_type);
    pos = common::EncodeVarint(pos, _token_length);

    buffer->MoveWritePt(pos - span.GetStart());
    buffer->Write(_token, _token_length);
    return true;
}

bool NewTokenFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    auto span = buffer->GetReadSpan();
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    if (with_type) {
        pos = common::FixedDecodeUint16(pos, end, _frame_type);
        if (_frame_type != FT_NEW_TOKEN) {
            return false;
        }
    }
    pos = common::DecodeVarint(pos, end, _token_length);

    buffer->MoveReadPt(pos - span.GetStart());
    if (_token_length > buffer->GetDataLength()) {
        common::LOG_ERROR("insufficient remaining data. remain_size:%d, need_size:%d", buffer->GetDataLength(), _token_length);
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
}