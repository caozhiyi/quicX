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
    pos = common::FixedEncodeUint16(pos, frame_type_);
    pos = common::EncodeVarint(pos, token_length_);

    buffer->MoveWritePt(pos - span.GetStart());
    buffer->Write(token_, token_length_);
    return true;
}

bool NewTokenFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    auto span = buffer->GetReadSpan();
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    if (with_type) {
        pos = common::FixedDecodeUint16(pos, end, frame_type_);
        if (frame_type_ != FT_NEW_TOKEN) {
            return false;
        }
    }
    pos = common::DecodeVarint(pos, end, token_length_);

    buffer->MoveReadPt(pos - span.GetStart());
    if (token_length_ > buffer->GetDataLength()) {
        common::LOG_ERROR("insufficient remaining data. remain_size:%d, need_size:%d", buffer->GetDataLength(), token_length_);
        return false;
    }
    
    token_ = pos;
    buffer->MoveReadPt(token_length_);
    return true;
}

uint32_t NewTokenFrame::EncodeSize() {
    return sizeof(NewTokenFrame);
}

}
}