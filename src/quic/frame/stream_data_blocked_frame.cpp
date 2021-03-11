#include "common/decode/decode.h"
#include "stream_data_blocked_frame.h"
#include "common/buffer/buffer_queue.h"
#include "common/decode/normal_decode.h"


namespace quicx {

StreamDataBlockedFrame::StreamDataBlockedFrame():
    Frame(FT_STREAM_DATA_BLOCKED),
    _stream_id(0),
    _data_limit(0) {

}

StreamDataBlockedFrame::~StreamDataBlockedFrame() {

}

bool StreamDataBlockedFrame::Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    uint16_t size = EncodeSize();

    char* data = alloter->PoolMalloc<char>(size);

    char* pos = EncodeFixed<uint16_t>(data, _frame_type);
    pos = EncodeVarint(pos, _stream_id);
    pos = EncodeVarint(pos, _data_limit);

    buffer->Write(data, pos - data);

    alloter->PoolFree(data, size);
    return true;
}

bool StreamDataBlockedFrame::Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type) {
    uint16_t size = EncodeSize();

    char* data = alloter->PoolMalloc<char>(size);
    uint32_t len = buffer->ReadNotMovePt(data, size);
    
    char* pos = nullptr;
    if (with_type) {
        uint16_t type = 0;
        pos = DecodeFixed<uint16_t>(data, data + size, type);
        _frame_type = (FrameType)type;
    }
    
    pos = DecodeVirint(pos, data + size, _stream_id);
    pos = DecodeVirint(pos, data + size, _data_limit);

    buffer->MoveReadPt(pos - data);
    alloter->PoolFree(data, size);
    return true;
}

uint32_t StreamDataBlockedFrame::EncodeSize() {
    return sizeof(StreamDataBlockedFrame);
}

}