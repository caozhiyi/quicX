#include "path_response_frame.h"
#include "common/decode/normal_decode.h"
#include "common/buffer/buffer_interface.h"
#include "common/alloter/alloter_interface.h"

namespace quicx {


PathResponseFrame::PathResponseFrame(): 
    Frame(FT_PATH_RESPONSE) {

}

PathResponseFrame::~PathResponseFrame() {

}

bool PathResponseFrame::Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    uint16_t size = EncodeSize();

    char* data = alloter->PoolMalloc<char>(size);

    char* pos = EncodeFixed<uint16_t>(data, _frame_type);
    buffer->Write(data, pos - data);
    buffer->Write(_data, __path_data_length);

    alloter->PoolFree(data, size);
    return true;
}

bool PathResponseFrame::Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type) {
    uint16_t size = EncodeSize();

    char* data = alloter->PoolMalloc<char>(size);
    uint32_t len = buffer->ReadNotClear(data, size);
    
    char* pos = nullptr;
    if (with_type) {
        uint16_t type = 0;
        pos = DecodeFixed<uint16_t>(data, data + size, type);
        _frame_type = (FrameType)type;
    }

    memcpy(_data, pos, __path_data_length);
    pos += 8;

    buffer->Clear(pos - data);
    alloter->PoolFree(data, size);
    return true;
}

uint32_t PathResponseFrame::EncodeSize() {
    return sizeof(PathResponseFrame);
}

void PathResponseFrame::SetData(char* data) {
    memcpy(_data, data, __path_data_length);
}

}