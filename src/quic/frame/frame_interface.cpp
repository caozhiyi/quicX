#include "frame_interface.h"
#include "common/decode/normal_decode.h"
#include "common/buffer/buffer_queue.h"

namespace quicx {

Frame::Frame(FrameType ft): _frame_type(ft) {
    
}

Frame::~Frame() {

}

FrameType Frame::GetType() { 
    return _frame_type; 
}

bool Frame::Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) {
    uint16_t size = EncodeSize();

    char* data = alloter->PoolMalloc<char>(size);
    char* pos = EncodeFixed<uint16_t>(data, _frame_type);

    buffer->Write(data, pos - data);
    alloter->PoolFree(data, size);
    return true;
}

bool Frame::Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type) {
    if (with_type) {
        uint16_t size = EncodeSize();

        char* data = alloter->PoolMalloc<char>(size);
        uint32_t len = buffer->Read(data, size);
        if (len != size) {
            return false;
        }

        uint16_t type = 0;
        DecodeFixed<uint16_t>(data, data + size, type);
        _frame_type = (FrameType)type;

        alloter->PoolFree(data, size);
    
    }
    return true;
}

uint32_t Frame::EncodeSize() {
    return sizeof(Frame);
}

}