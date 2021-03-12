#include "max_stream_data_frame.h"
#include "common/decode/decode.h"
#include "common/decode/normal_decode.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"

namespace quicx {


MaxStreamDataFrame::MaxStreamDataFrame():
    Frame(FT_MAX_STREAM_DATA),
    _stream_id(0),
    _maximum_data(0) {

}

MaxStreamDataFrame::~MaxStreamDataFrame() {

}

bool MaxStreamDataFrame::Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    uint16_t size = EncodeSize();

    char* data = alloter->PoolMalloc<char>(size);

    char* pos = EncodeFixed<uint16_t>(data, _frame_type);
    pos = EncodeVarint(pos, _stream_id);
    pos = EncodeVarint(pos, _maximum_data);

    buffer->Write(data, pos - data);

    alloter->PoolFree(data, size);
    return true;
}

bool MaxStreamDataFrame::Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type) {
    uint16_t size = EncodeSize();

    char* data = alloter->PoolMalloc<char>(size);
    buffer->ReadNotMovePt(data, size);
    
    char* pos = data;
    if (with_type) {
        pos = DecodeFixed<uint16_t>(data, data + size, _frame_type);
    }

    pos = DecodeVirint(pos, data + size, _stream_id);
    pos = DecodeVirint(pos, data + size, _maximum_data);

    buffer->MoveReadPt(pos - data);
    alloter->PoolFree(data, size);
    return true;
}

uint32_t MaxStreamDataFrame::EncodeSize() {
    return sizeof(MaxStreamDataFrame);
}

}