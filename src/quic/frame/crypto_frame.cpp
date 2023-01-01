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
    auto pos_pair = buffer->GetWritePair();
    auto remain_size = pos_pair.second - pos_pair.first;
    if (need_size > remain_size) {
        LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }

    uint8_t* pos = pos_pair.first;
    pos = FixedEncodeUint16(pos, _frame_type);
    pos = EncodeVarint(pos, _offset);
    pos = EncodeVarint(pos, _length);

    buffer->MoveWritePt(pos - pos_pair.first);

    buffer->Write(_data, _length);
    return true;
}

bool CryptoFrame::Decode(std::shared_ptr<IBufferRead> buffer, bool with_type) {
    auto pos_pair = buffer->GetReadPair();
    uint8_t* pos = pos_pair.first;
    
    if (with_type) {
        pos = FixedDecodeUint16(pos, pos_pair.second, _frame_type);
        if (_frame_type != FT_CRYPTO){
            return false;
        }
        
    }
    pos = DecodeVarint(pos, pos_pair.second, _offset);
    pos = DecodeVarint(pos, pos_pair.second, _length);
    buffer->MoveReadPt(pos - pos_pair.first);
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