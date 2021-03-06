#include "max_data_frame.h"
#include "common/decode/decode.h"
#include "common/decode/normal_decode.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"

namespace quicx {

MaxDataFrame::MaxDataFrame(): 
    Frame(FT_MAX_DATA),
    _maximum_data(0) {

}

MaxDataFrame::~MaxDataFrame() {

}

bool MaxDataFrame::Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    uint16_t size = EncodeSize();

    char* data = alloter->PoolMalloc<char>(size);
    char* pos = data;

    pos = EncodeFixed<uint16_t>(data, _frame_type);
    pos = EncodeVarint(pos, _maximum_data);

    buffer->Write(data, pos - data);
    alloter->PoolFree(data, size);
    return true;
}

bool MaxDataFrame::Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type) {
    uint16_t size = EncodeSize();
    char* data = alloter->PoolMalloc<char>(size);
    buffer->ReadNotMovePt(data, size);
    char* pos = data;

    if (with_type) {
        pos = DecodeFixed<uint16_t>(data, data + size, _frame_type);
        if (_frame_type != FT_MAX_DATA) {
            return false;
        }
    }
    pos = DecodeVirint(pos, data + size, _maximum_data);

    buffer->MoveReadPt(pos - data);
    alloter->PoolFree(data, size);
    return true;
}

uint32_t MaxDataFrame::EncodeSize() {
    return sizeof(MaxDataFrame);
}

}