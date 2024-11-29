#include "common/log/log.h"
#include "common/decode/decode.h"
#include "quic/frame/stream_frame.h"

namespace quicx {
namespace quic {

StreamFrame::StreamFrame():
    IStreamFrame(FT_STREAM),
    offset_(0),
    length_(0) {

}

StreamFrame::StreamFrame(uint16_t frame_type):
    IStreamFrame(frame_type),
    offset_(0) {

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
    pos = common::FixedEncodeUint16(pos, frame_type_);
    pos = common::EncodeVarint(pos, stream_id_);
    if (HasOffset()) {
        pos = common::EncodeVarint(pos, offset_);
    }
    if (HasLength()) {
        pos = common::EncodeVarint(pos, length_);
    }
    buffer->MoveWritePt(pos - span.GetStart());
    buffer->Write(data_, length_);
    return true;
}

bool StreamFrame::Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type) {
    auto span = buffer->GetReadSpan();
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();

    if (with_type) {
        pos = common::FixedDecodeUint16(pos, end, frame_type_);
    }
    pos = common::DecodeVarint(pos, end, stream_id_);
    if (HasOffset()) {
        pos = common::DecodeVarint(pos, end, offset_);
    }
    if (HasLength()) {
        pos = common::DecodeVarint(pos, end, length_);
    }
    buffer->MoveReadPt(pos - span.GetStart());

    if (length_ > buffer->GetDataLength()) {
        common::LOG_ERROR("insufficient remaining data. remain_size:%d, need_size:%d", buffer->GetDataLength(), length_);
        return false;
    }
    
    data_ = pos;
    buffer->MoveReadPt(length_);
    return true;
}

uint32_t StreamFrame::EncodeSize() {
    return sizeof(StreamFrame);
}

void StreamFrame::SetOffset(uint64_t offset) {
    offset_ = offset;
    frame_type_ |= SFF_OFF;
}

void StreamFrame::SetData(uint8_t* data, uint32_t send_len) {
    if (send_len > 0) {
        frame_type_ |= SFF_LEN;
        data_ = data;
        length_ = send_len;
    }
}

bool StreamFrame::IsStreamFrame(uint16_t frame_type) {
    return (frame_type & ~SFF_MASK) == FT_STREAM;
}

}
}