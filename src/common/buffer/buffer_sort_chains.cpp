#include "common/buffer/buffer_sort_chains.h"

namespace quicx {

BufferSortChains::BufferSortChains(std::shared_ptr<BlockMemoryPool>& alloter):
    _cur_write_offset(0),
    _read_pos(nullptr),
    _write_pos(nullptr),
    _alloter(alloter) {

}

BufferSortChains::~BufferSortChains() {

}

uint32_t BufferSortChains::ReadNotMovePt(uint8_t* data, uint32_t len) {
    if (len == 0 || data == nullptr) {
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

uint32_t BufferSortChains::MoveReadPt(int32_t len) {
    if (len == 0) {
        return 0;
    }

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

uint32_t BufferSortChains::MoveWritePt(int32_t len) {
    return 0;
}

uint32_t BufferSortChains::Read(uint8_t* data, uint32_t len) {
    if (len == 0 || data == nullptr) {
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

uint32_t BufferSortChains::Write(uint64_t offset, uint8_t* data, uint32_t len) {
    return 0;
}

uint32_t BufferSortChains::MoveWritePt(uint64_t offset) {
    int32_t move_len = 0;
    bool move_next;
    if (offset > _cur_write_offset) {
        move_next = true;
        move_len = offset - _cur_write_offset;
    } else {
        move_next = false;
        move_len = _cur_write_offset - offset;
    }

    if (move_len == 0) {
        return move_len;
    }

    return 0;
}

}
