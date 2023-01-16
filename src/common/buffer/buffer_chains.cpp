#include "common/buffer/buffer_chains.h"
#include "common/buffer/buffer_read_write.h"

namespace quicx {

BufferChains::BufferChains(std::shared_ptr<BlockMemoryPool>& alloter):
    _alloter(alloter) {
    _read_pos = _buffer_list.end();
    _write_pos = _buffer_list.end();
}

BufferChains::~BufferChains() {

}

uint32_t BufferChains::ReadNotMovePt(const uint8_t* data, uint32_t len) {
    if (data == nullptr) {
        return 0;
    }
    uint32_t size = 0;
    for (auto iter = _read_pos; iter != _buffer_list.end(); iter++) {
        size += (*iter)->ReadNotMovePt(data, len - size);
        if (size >= len) {
            break;
        }
    }
    return size;
}

uint32_t BufferChains::MoveReadPt(int32_t len) {
    uint32_t size = 0;
    for (auto iter = _read_pos; iter != _buffer_list.end(); iter++) {
        size += (*iter)->MoveReadPt(len - size);
        if (size >= len) {
            break;
        }
    }
    return size;
}

uint32_t BufferChains::Read(uint8_t* data, uint32_t len) {
    if (data == nullptr) {
        return 0;
    }
    uint32_t size = 0;
    for (auto iter = _read_pos; iter != _buffer_list.end(); iter++) {
        size += (*iter)->Read(data, len - size);
        if (size >= len) {
            break;
        }
    }
    return size;
}

uint32_t BufferChains::GetDataLength() {

}

std::vector<std::shared_ptr<IBufferRead>> BufferChains::GetReadBuffers() {

}


uint32_t BufferChains::Write(const uint8_t* data, uint32_t len) {

}

uint32_t BufferChains::GetFreeLength() {

}

uint32_t BufferChains::MoveWritePt(int32_t len) {

}

std::vector<std::shared_ptr<IBufferWrite>> BufferChains::GetWriteBuffers(uint32_t len) {

}

uint32_t BufferChains::InnerRead(BufferOperation op) {

}

uint32_t BufferChains::InnerWrite(BufferOperation op) {

}

void BufferChains::Clear() {

}

}
