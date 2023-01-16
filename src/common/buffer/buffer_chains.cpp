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

uint32_t BufferChains::ReadNotMovePt(uint8_t* data, uint32_t len) {
    if (data == nullptr) {
        return 0;
    }
    
    auto end_pos = GetReadEndPos();
    uint32_t size = 0;
    for (auto iter = _read_pos; iter != end_pos; iter++) {
        size += (*iter)->ReadNotMovePt(data, len - size);
        if (size >= len) {
            break;
        }
    }
    return size;
}

uint32_t BufferChains::MoveReadPt(int32_t len) {
    auto end_pos = GetReadEndPos();
    uint32_t size = 0;
    for (auto iter = _read_pos; iter != end_pos; iter++) {
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
    
    auto end_pos = GetReadEndPos();
    uint32_t size = 0;
    for (auto iter = _read_pos; iter != end_pos; iter++) {
        size += (*iter)->Read(data, len - size);
        if (size >= len) {
            break;
        }
    }
    return size;
}

uint32_t BufferChains::GetDataLength() {
    auto end_pos = GetReadEndPos();
    uint32_t size = 0;
    for (auto iter = _read_pos; iter != end_pos; iter++) {
        size += (*iter)->GetDataLength();
    }
    return size;
}

std::vector<std::shared_ptr<IBufferRead>> BufferChains::GetReadBuffers() {
    std::vector<std::shared_ptr<IBufferRead>> ret;
    auto end_pos = GetReadEndPos();
    for (auto iter = _read_pos; iter != end_pos; iter++) {
        ret.push_back(*iter);
    }
    return ret;
}


uint32_t BufferChains::Write(const uint8_t* data, uint32_t len) {

}

uint32_t BufferChains::GetFreeLength() {

}

uint32_t BufferChains::MoveWritePt(int32_t len) {

}

std::vector<std::shared_ptr<IBufferWrite>> BufferChains::GetWriteBuffers(uint32_t len) {

}

std::list<std::shared_ptr<BufferReadWrite>>::iterator BufferChains::GetReadEndPos() {
    std::list<std::shared_ptr<BufferReadWrite>>::iterator end_pos;
    if (_write_pos != _buffer_list.end()) {
        if ((*_write_pos)->GetDataLength() > 0) {
            end_pos = _write_pos;
            end_pos++;

        } else {
            end_pos = _write_pos;
        }
        
    } else {
        end_pos = _buffer_list.end();
    }
    return end_pos;
}

void BufferChains::Clear() {

}

}
