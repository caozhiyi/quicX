#include "common/decode/decode.h"
#include "retire_connection_id_frame.h"
#include "common/buffer/buffer_queue.h"
#include "common/decode/normal_decode.h"

namespace quicx {

RetireConnectionIDFrame::RetireConnectionIDFrame():
    Frame(FT_RETIRE_CONNECTION_ID),
    _sequence_number(0) {

}

RetireConnectionIDFrame::~RetireConnectionIDFrame() {

}

bool RetireConnectionIDFrame::Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    uint16_t size = EncodeSize();

    char* data = alloter->PoolMalloc<char>(size);

    char* pos = EncodeFixed<uint16_t>(data, _frame_type);
    pos = EncodeVarint(pos, _sequence_number);

    buffer->Write(data, pos - data);
    alloter->PoolFree(data, size);
    return true;
}

bool RetireConnectionIDFrame::Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type) {
    uint16_t size = EncodeSize();

    char* data = alloter->PoolMalloc<char>(size);
    uint32_t len = buffer->ReadNotMovePt(data, size);
    
    char* pos = nullptr;
    if (with_type) {
        uint16_t type = 0;
        pos = DecodeFixed<uint16_t>(data, data + size, type);
        _frame_type = (FrameType)type;
    }
    pos = DecodeVirint(pos, data + size, _sequence_number);

    buffer->MoveReadPt(pos - data);
    alloter->PoolFree(data, size);
    return true;
}

uint32_t RetireConnectionIDFrame::EncodeSize() {
    return sizeof(RetireConnectionIDFrame);
}
  
}