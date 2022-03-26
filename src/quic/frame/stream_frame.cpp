#include "common/log/log.h"
#include "quic/frame/stream_frame.h"
#include "common/decode/normal_decode.h"

namespace quicx {

StreamFrame::StreamFrame():
    IFrame(FT_STREAM),
    _offset(0),
    _length(0) {

}

StreamFrame::StreamFrame(uint16_t frame_type):
    IFrame(frame_type),
    _offset(0) {

}

StreamFrame::~StreamFrame() {

}

bool StreamFrame::Encode(std::shared_ptr<IBufferWriteOnly> buffer) {
    uint16_t need_size = EncodeSize();
    auto pos_pair = buffer->GetWritePair();
    auto remain_size = pos_pair.second - pos_pair.first;
    if (need_size > remain_size) {
        LOG_ERROR("insufficient remaining cache space. remain_size:%d, need_size:%d", remain_size, need_size);
        return false;
    }

    char* pos = pos_pair.first;
    pos = EncodeFixed<uint16_t>(pos, _frame_type);
    pos = EncodeVarint(pos, _stream_id);
    if (HasOffset()) {
        pos = EncodeVarint(pos, _offset);
    }
    if (HasLength()) {
        pos = EncodeVarint(pos, _length);
    }
    buffer->MoveWritePt(pos - pos_pair.first);
    buffer->Write(_data, _length);
    return true;
}

bool StreamFrame::Decode(std::shared_ptr<IBufferReadOnly> buffer, bool with_type) {
    auto pos_pair = buffer->GetReadPair();
    char* pos = pos_pair.first;

    if (with_type) {
        pos = DecodeFixed<uint16_t>(pos, pos_pair.second, _frame_type);
        if (_frame_type < FT_STREAM || _frame_type > FT_STREAM_MAX) {
            return false;
        }
    }
    pos = DecodeVarint(pos, pos_pair.second, _stream_id);
    if (HasOffset()) {
        pos = DecodeVarint(pos, pos_pair.second, _offset);
    }
    if (HasLength()) {
        pos = DecodeVarint(pos, pos_pair.second, _length);
    }
    buffer->MoveReadPt(pos - pos_pair.first);

    if (_length > buffer->GetCanReadLength()) {
        LOG_ERROR("insufficient remaining data. remain_size:%d, need_size:%d", buffer->GetCanReadLength(), _length);
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

void StreamFrame::SetData(char* data, uint32_t send_len) {
    if (send_len > 0) {
        _frame_type |= SFF_LEN;
        _data = data;
        _length = send_len;
    }
}

}