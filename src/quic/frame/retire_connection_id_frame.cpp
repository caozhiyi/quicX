#include "common/decode/decode.h"
#include "retire_connection_id_frame.h"
#include "common/decode/normal_decode.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"

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
    buffer->ReadNotMovePt(data, size);
    
    char* pos = data;
    if (with_type) {
        pos = DecodeFixed<uint16_t>(data, data + size, _frame_type);
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