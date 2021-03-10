#include "max_streams_frame.h"
#include "common/decode/decode.h"
#include "common/buffer/buffer_queue.h"
#include "common/decode/normal_decode.h"

namespace quicx {


MaxStreamsFrame::MaxStreamsFrame(): 
    Frame(FT_MAX_STREAMS),
    _maximum_streams(0) {

}

MaxStreamsFrame::~MaxStreamsFrame() {

}

bool MaxStreamsFrame::Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    uint16_t size = EncodeSize();

    char* data = alloter->PoolMalloc<char>(size);

    char* pos = EncodeFixed<uint16_t>(data, _frame_type);
    pos = EncodeVarint(pos, _maximum_streams);

    buffer->Write(data, pos - data);
    alloter->PoolFree(data, size);
    return true;
}

bool MaxStreamsFrame::Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type) {
    uint16_t size = EncodeSize();

    char* data = alloter->PoolMalloc<char>(size);
    uint32_t len = buffer->ReadNotClear(data, size);
    
    char* pos = nullptr;
    if (with_type) {
        uint16_t type = 0;
        pos = DecodeFixed<uint16_t>(data, data + size, type);
        _frame_type = (FrameType)type;
    }
    pos = DecodeVirint(pos, data + size, _maximum_streams);

    buffer->Clear(pos - data);
    alloter->PoolFree(data, size);
    return true;
}

uint32_t MaxStreamsFrame::EncodeSize() {
    return sizeof(MaxStreamsFrame);
}

}