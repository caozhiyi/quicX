#include "data_blocked_frame.h"
#include "common/decode/decode.h"
#include "common/buffer/buffer_queue.h"
#include "common/decode/normal_decode.h"

namespace quicx {

DataBlockedFrame::DataBlockedFrame():
    Frame(FT_DATA_BLOCKED),
    _data_limit(0) {

}

DataBlockedFrame::~DataBlockedFrame() {

}

bool DataBlockedFrame::Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    uint16_t size = EncodeSize();

    char* data = alloter->PoolMalloc<char>(size);
    char* pos = EncodeFixed<uint16_t>(data, _frame_type);
    pos = EncodeVarint(pos, _data_limit);

    buffer->Write(data, pos - data);
    alloter->PoolFree(data, size);
    return true;
}

bool DataBlockedFrame::Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type) {
    uint16_t size = EncodeSize();

    char* data = alloter->PoolMalloc<char>(size);
    uint32_t len = buffer->ReadNotMovePt(data, size);
    
    char* pos = nullptr;
    if (with_type) {
        pos = DecodeFixed<uint16_t>(data, data + size, _frame_type);
    }
    pos = DecodeVirint(pos, data + size, _data_limit);

    buffer->MoveReadPt(pos - data);
    alloter->PoolFree(data, size);
    return true;
}

uint32_t DataBlockedFrame::EncodeSize() {
    return sizeof(DataBlockedFrame);
}

}
