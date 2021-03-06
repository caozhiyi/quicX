#include "stream_frame.h"
#include "common/buffer/buffer_queue.h"
#include "common/decode/normal_decode.h"

namespace quicx {

StreamFrame::StreamFrame():
    Frame(FT_STREAM),
    _offset(0) {

}

StreamFrame::StreamFrame(uint16_t frame_type):
    Frame(frame_type),
    _offset(0) {

}

StreamFrame::~StreamFrame() {

}

bool StreamFrame::Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    uint16_t size = EncodeSize();
    char* data = alloter->PoolMalloc<char>(size);
    char* pos = data;

    pos = EncodeFixed<uint16_t>(pos, _frame_type);
    pos = EncodeVarint(pos, _stream_id);
    if (HasOffset()) {
        pos = EncodeVarint(pos, _offset);
    }
    if (HasLength()) {
        pos = EncodeVarint(pos, _data->GetCanReadLength());
    }
    buffer->Write(data, pos - data);
    alloter->PoolFree(data, size);

    buffer->Write(_data, _send_length);
    return true;
}

bool StreamFrame::Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type) {
    uint16_t size = EncodeSize();
    char* data = alloter->PoolMalloc<char>(size);
    buffer->ReadNotMovePt(data, size);
    char* pos = data;

    if (with_type) {
        pos = DecodeFixed<uint16_t>(data, data + size, _frame_type);
        if (_frame_type < FT_STREAM || _frame_type > FT_STREAM_MAX) {
            return false;
        }
    }
    pos = DecodeVirint(pos, data + size, _stream_id);
    if (HasOffset()) {
        pos = DecodeVirint(pos, data + size, _offset);
    }
    uint32_t length = 0;
    if (HasLength()) {
        pos = DecodeVirint(pos, data + size, length);
    }
    buffer->MoveReadPt(pos - data);
    alloter->PoolFree(data, size);

    _data = std::make_shared<BufferQueue>(buffer->GetBlockMemoryPool(), alloter);
    if (buffer->Read(_data, length) != length) {
        return false;
    }
    return true;
}

uint32_t StreamFrame::EncodeSize() {
    return sizeof(StreamFrame);
}

void StreamFrame::SetOffset(uint64_t offset) {
    _offset = offset;
    _frame_type |= SFF_OFF;
}

void StreamFrame::SetData(std::shared_ptr<Buffer> data, uint32_t send_len) {
    if (send_len == 0) {
        send_len = data->GetCanReadLength();
    }
    if (send_len > 0) {
        _frame_type |= SFF_LEN;
        _data = data;
        _send_length = send_len;
    }
}

}