#include "common/decode/decode.h"
#include "streams_blocked_frame.h"
#include "common/buffer/buffer_queue.h"
#include "common/decode/normal_decode.h"

namespace quicx {


StreamsBlockedFrame::StreamsBlockedFrame():
    Frame(FT_STREAMS_BLOCKED),
    _stream_limit(0) {

}

StreamsBlockedFrame::~StreamsBlockedFrame() {

}

bool StreamsBlockedFrame::Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    uint16_t size = EncodeSize();

    char* data = alloter->PoolMalloc<char>(size);

    char* pos = EncodeFixed<uint16_t>(data, _frame_type);
    pos = EncodeVarint(pos, _stream_limit);

    buffer->Write(data, pos - data);
    alloter->PoolFree(data, size);
    return true;
}

bool StreamsBlockedFrame::Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type) {
    uint16_t size = EncodeSize();

    char* data = alloter->PoolMalloc<char>(size);
    buffer->ReadNotMovePt(data, size);
    
    char* pos = data;
    if (with_type) {
        pos = DecodeFixed<uint16_t>(data, data + size, _frame_type);
    }
    pos = DecodeVirint(pos, data + size, _stream_limit);

    buffer->MoveReadPt(pos - data);
    alloter->PoolFree(data, size);
    return true;
}

uint32_t StreamsBlockedFrame::EncodeSize() {
    return sizeof(StreamsBlockedFrame);
}

}
