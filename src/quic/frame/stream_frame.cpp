#include "common/log/log.h"
#include "common/decode/decode.h"
#include "quic/frame/stream_frame.h"

namespace quicx {
namespace quic {

StreamFrame::StreamFrame():
    IStreamFrame(FT_STREAM),
    _offset(0),
    _length(0) {

}

StreamFrame::StreamFrame(uint16_t frame_type):
    IStreamFrame(frame_type),
    _offset(0) {

}

StreamFrame::~StreamFrame() {

}

bool StreamFrame::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    uint16_t need_size = EncodeSize();
    auto span = buffer->GetWriteSpan();
    auto remain_size = span.GetLength();
    if (need_size > remain_size) {
        common::LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }

    uint8_t* pos = span.GetStart();
    pos = common::FixedEncodeUint16(pos, _frame_type);
    pos = common::EncodeVarint(pos, _stream_id);
    if (HasOffset()) {
        pos = common::EncodeVarint(pos, _offset);
    }
    if (HasLength()) {
        pos = common::EncodeVarint(pos, _length);
    }
    buffer->MoveWritePt(pos - span.GetStart());
    buffer->Write(_data, _length);
    return true;
}

bool StreamFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    auto span = buffer->GetReadSpan();
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    if (with_type) {
        pos = common::FixedDecodeUint16(pos, end, _frame_type);
    }
    pos = common::DecodeVarint(pos, end, _stream_id);
    if (HasOffset()) {
        pos = common::DecodeVarint(pos, end, _offset);
    }
    if (HasLength()) {
        pos = common::DecodeVarint(pos, end, _length);
    }
    buffer->MoveReadPt(pos - span.GetStart());

    if (_length > buffer->GetDataLength()) {
        common::LOG_ERROR("insufficient remaining data. remain_size:%d, need_size:%d", buffer->GetDataLength(), _length);
        return false;
    }
    
    _data = pos;
    buffer->MoveReadPt(_length);
    return true;
}

uint32_t StreamFrame::EncodeSize() {
    return sizeof(StreamFrame);
}

void StreamFrame::SetOffset(uint64_t offset) {
    _offset = offset;
    _frame_type |= SFF_OFF;
}

void StreamFrame::SetData(uint8_t* data, uint32_t send_len) {
    if (send_len > 0) {
        _frame_type |= SFF_LEN;
        _data = data;
        _length = send_len;
    }
}

bool StreamFrame::IsStreamFrame(uint16_t frame_type) {
    return (frame_type & ~SFF_MASK) == FT_STREAM;
}

}
}