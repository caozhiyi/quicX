#include "common/log/log.h"
#include "common/decode/decode.h"
#include "quic/frame/crypto_frame.h"

namespace quicx {

CryptoFrame::CryptoFrame():
    IFrame(FT_CRYPTO),
    _offset(0) {

}

CryptoFrame::~CryptoFrame() {

}

bool CryptoFrame::Encode(std::shared_ptr<IBufferWrite> buffer) {
    uint16_t need_size = EncodeSize();
    auto span = buffer->GetWriteSpan();
    auto remain_size = span.GetLength();
    if (need_size > remain_size) {
        LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }

    uint8_t* pos = span.GetStart();
    pos = FixedEncodeUint16(pos, _frame_type);
    pos = EncodeVarint(pos, _offset);
    pos = EncodeVarint(pos, _length);

    buffer->MoveWritePt(pos - span.GetStart());

    buffer->Write(_data, _length);
    return true;
}

bool CryptoFrame::Decode(std::shared_ptr<IBufferRead> buffer, bool with_type) {
    auto span = buffer->GetReadSpan();
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    if (with_type) {
        pos = FixedDecodeUint16(pos, end, _frame_type);
        if (_frame_type != FT_CRYPTO){
            return false;
        }
        
    }
    pos = DecodeVarint(pos, end, _offset);
    pos = DecodeVarint(pos, end, _length);
    buffer->MoveReadPt(pos - span.GetStart());
    if (_length > buffer->GetDataLength()) {
        LOG_ERROR("insufficient remaining data. remain_size:%d, need_size:%d", buffer->GetDataLength(), _length);
        return false;
    }
    
    _data = pos;
    buffer->MoveReadPt(_length);
    return true;
}

uint32_t CryptoFrame::EncodeSize() {
    return sizeof(CryptoFrame) + _length;
}

}