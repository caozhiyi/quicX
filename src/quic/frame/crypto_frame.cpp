#include "common/log/log.h"
#include "common/decode/decode.h"
#include "quic/frame/crypto_frame.h"

namespace quicx {
namespace quic {

CryptoFrame::CryptoFrame():
    IFrame(FT_CRYPTO),
    offset_(0) {

}

CryptoFrame::~CryptoFrame() {

}

bool CryptoFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    uint16_t need_size = EncodeSize();
    auto span = buffer->GetWriteSpan();
    auto remain_size = span.GetLength();
    if (need_size > remain_size) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }

    uint8_t* pos = span.GetStart();
    pos = common::FixedEncodeUint16(pos, frame_type_);
    pos = common::EncodeVarint(pos, offset_);
    pos = common::EncodeVarint(pos, length_);

    buffer->MoveWritePt(pos - span.GetStart());

    buffer->Write(data_, length_);
    return true;
}

bool CryptoFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    auto span = buffer->GetReadSpan();
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    if (with_type) {
        pos = common::FixedDecodeUint16(pos, end, frame_type_);
        if (frame_type_ != FT_CRYPTO){
            return false;
        }
        
    }
    pos = common::DecodeVarint(pos, end, offset_);
    pos = common::DecodeVarint(pos, end, length_);
    buffer->MoveReadPt(pos - span.GetStart());
    if (length_ > buffer->GetDataLength()) {
        common::LOG_ERROR("insufficient remaining data. remain_size:%d, need_size:%d", buffer->GetDataLength(), length_);
        return false;
    }
    
    data_ = pos;
    buffer->MoveReadPt(length_);
    return true;
}

uint32_t CryptoFrame::EncodeSize() {
    return sizeof(CryptoFrame) + length_;
}

}
}