#include "reset_stream_frame.h"
#include "common/decode/decode.h"
#include "common/decode/normal_decode.h"
#include "common/buffer/buffer_queue.h"

namespace quicx {

ResetStreamFrame::ResetStreamFrame(): 
    Frame(FT_RESET_STREAM),
    _stream_id(0),
    _app_error_code(0), 
    _final_size(0) {

}

ResetStreamFrame::~ResetStreamFrame() {

}

bool ResetStreamFrame::Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    uint16_t size = EncodeSize();

    char* data = alloter->PoolMalloc<char>(size);

    char* pos = EncodeFixed<uint16_t>(data, _frame_type);
    pos = EncodeVarint(pos, _stream_id);
    pos = EncodeVarint(pos, _app_error_code);
    pos = EncodeVarint(pos, _final_size);

    buffer->Write(data, pos - data);
    alloter->PoolFree(data, size);
    return true;
}

bool ResetStreamFrame::Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type) {
    uint16_t size = EncodeSize();

    char* data = alloter->PoolMalloc<char>(size);
    uint32_t len = buffer->ReadNotClear(data, size);
    
    char* pos = nullptr;
    if (with_type) {
        uint16_t type = 0;
        pos = DecodeFixed<uint16_t>(data, data + size, type);
        _frame_type = (FrameType)type;
    }
    pos = DecodeVirint(pos, data + size, _stream_id);
    pos = DecodeVirint(pos, data + size, _app_error_code);
    pos = DecodeVirint(pos, data + size, _final_size);

    buffer->Clear(pos - data);
    alloter->PoolFree(data, size);
    return true;
}

uint32_t ResetStreamFrame::EncodeSize() {
    return sizeof(ResetStreamFrame);
}

}