#include "common/buffer/buffer_sort_chains.h"

namespace quicx {

BufferSortChains::BufferSortChains(std::shared_ptr<BlockMemoryPool>& alloter):
    BufferChains(alloter) {

}

BufferSortChains::~BufferSortChains() {

}

uint32_t BufferSortChains::MoveReadPt(int32_t len) {
    uint32_t size = BufferChains::MoveReadPt(len);
    auto iter = _segment_map.begin();
    if (iter == _segment_map.end()) {
        return size;
    }
    
    uint64_t start_offset = iter->first + size;
    while (iter != _segment_map.end()) {
        if (iter->first < start_offset) {
            iter = _segment_map.erase(iter);
        
        } else {
            break;
        }
    }

    if (iter == _segment_map.end()) {
        return size;
    }
    _segment_map[start_offset] = ST_START;
    return size;
}

uint32_t BufferSortChains::Read(uint8_t* data, uint32_t len) {
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

uint32_t BufferSortChains::GetDataLength() {
    uint32_t size = 0;
    for (auto iter = _read_pos; iter && iter->GetDataLength() > 0; iter = iter->GetNext()) {
        size += iter->GetDataLength();
    }
    return size;
}

std::shared_ptr<BufferBlock> BufferSortChains::GetReadBuffers() {
    return _read_pos;
}

uint32_t BufferSortChains::Write(uint8_t* data, uint32_t len) {
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

uint32_t BufferSortChains::GetFreeLength() {
    uint32_t size = 0;
    for (auto iter = _write_pos; iter; iter = iter->GetNext()) {
        size += iter->GetFreeLength();
    }
    return size;
}

uint32_t BufferSortChains::MoveWritePt(int32_t len) {
    uint32_t size = 0;
    for (; _write_pos && _write_pos->GetFreeLength() > 0; _write_pos = _write_pos->GetNext()) {
        size += _write_pos->MoveWritePt(len - size);
        if (size >= len) {
            break;
        }
    }
    return size;
}

std::shared_ptr<BufferBlock> BufferSortChains::GetWriteBuffers(uint32_t len) {
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
