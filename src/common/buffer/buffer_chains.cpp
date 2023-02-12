#include "common/buffer/buffer.h"
#include "common/buffer/buffer_chains.h"

namespace quicx {

BufferChains::BufferChains(std::shared_ptr<BlockMemoryPool>& alloter):
    _read_pos(nullptr),
    _write_pos(nullptr),
    _alloter(alloter) {

}

BufferChains::~BufferChains() {

}

uint32_t BufferChains::ReadNotMovePt(uint8_t* data, uint32_t len) {
    if (data == nullptr) {
        return 0;
    }
    
    uint32_t size = 0;
    for (auto iter = _read_pos; iter && iter->GetDataLength() > 0; iter = iter->GetNext()) {
        size += iter->ReadNotMovePt(data + size, len - size);
        if (size >= len) {
            break;
        }
    }
    return size;
}

uint32_t BufferChains::MoveReadPt(int32_t len) {
    uint32_t size = 0;
    for (; _read_pos && _read_pos->GetDataLength() > 0; _read_pos = _read_pos->GetNext()) {
        size += _read_pos->MoveReadPt(len - size);
        if (size >= len) {
            break;
        }
        _buffer_list.PopFront();
    }

    return size;
}

uint32_t BufferChains::Read(uint8_t* data, uint32_t len) {
    if (data == nullptr) {
        return 0;
    }

    uint32_t size = 0;
    for (; _read_pos && _read_pos->GetDataLength() > 0; _read_pos = _read_pos->GetNext()) {
        size += _read_pos->Read(data + size, len - size);
        if (size >= len) {
            break;
        }
        _buffer_list.PopFront();
    }
    return size;
}

uint32_t BufferChains::GetDataLength() {
    uint32_t size = 0;
    for (auto iter = _read_pos; iter && iter->GetDataLength() > 0; iter = iter->GetNext()) {
        size += iter->GetDataLength();
    }
    return size;
}

std::shared_ptr<BufferBlock> BufferChains::GetReadBuffers() {
    return _read_pos;
}

uint32_t BufferChains::Write(uint8_t* data, uint32_t len) {
    uint32_t offset = 0;
    while (offset < len) {
        if (!_write_pos || _write_pos->GetFreeLength() == 0) {
            _write_pos = std::make_shared<BufferBlock>(_alloter);
            _buffer_list.PushBack(_write_pos);
            if (!_read_pos) {
                _read_pos = _write_pos;
            }
        }
        offset += _write_pos->Write(data + offset, len - offset);
    }
    return offset;
}

uint32_t BufferChains::GetFreeLength() {
    uint32_t size = 0;
    for (auto iter = _write_pos; iter; iter = iter->GetNext()) {
        size += iter->GetFreeLength();
    }
    return size;
}

uint32_t BufferChains::MoveWritePt(int32_t len) {
    uint32_t size = 0;
    for (; _write_pos && _write_pos->GetFreeLength() > 0; _write_pos = _write_pos->GetNext()) {
        size += _write_pos->MoveWritePt(len - size);
        if (size >= len) {
            break;
        }
    }
    return size;
}

std::shared_ptr<BufferBlock> BufferChains::GetWriteBuffers(uint32_t len) {
    uint32_t offset = 0;
    std::shared_ptr<BufferBlock> cur_write = _write_pos;
    while (offset < len) {
        if (!cur_write || cur_write->GetFreeLength() == 0) {
            cur_write = std::make_shared<BufferBlock>(_alloter);
            _buffer_list.PushBack(cur_write);
            if (!_write_pos) {
                _write_pos = cur_write;
            }
        }
        offset += cur_write->GetFreeLength();
        cur_write = cur_write->GetNext();
    }
    return _write_pos;
}

}
