#include "common/buffer/buffer_sort_chains.h"

namespace quicx {

bool SortSegment::Insert(uint64_t offset, uint32_t len) {
    if (offset <= _cur_offset && _cur_offset != 0) {
        return false;
    }
    
    auto iter = _segment_map.upper_bound(offset);
    if (iter != _segment_map.end()) {
        if (iter != _segment_map.begin()) {
            iter--;
        }
        if (iter->second == ST_START) {
            return false;
        }
    }

    _segment_map[offset] = ST_START;
    _segment_map[offset + len] = ST_END;
    return true;
}

uint32_t SortSegment::Remove(uint32_t len) {
    uint64_t max_len = MaxSortLength();
    if (max_len == 0) {
        return 0;
    }
    auto iter = _segment_map.begin();
    if (iter == _segment_map.end()) {
        return 0;
    }
    
    max_len = max_len > len ? len : max_len;
    uint64_t start_offset = iter->first + max_len;
    uint32_t remove_size = 0;
    while (iter != _segment_map.end()) {
        if (iter->first <= start_offset) {
            remove_size = iter->first - _cur_offset;
            iter = _segment_map.erase(iter);
        
        } else {
            break;
        }
    }

    _cur_offset = start_offset;
    if (iter != _segment_map.end()) {
        _segment_map[start_offset] = ST_START;
    }
    return max_len;
}

uint64_t SortSegment::MaxSortLength() {
    auto iter = _segment_map.begin();
    for (;iter != _segment_map.end(); iter++) {
        if (iter->second == ST_END) {
            break;
        }
    }
    if (iter == _segment_map.end()) {
        return 0;
    }
    return iter->first - _cur_offset;
}

BufferSortChains::BufferSortChains(std::shared_ptr<BlockMemoryPool>& alloter, uint32_t max_size):
    _read_index(0),
    _write_index(0),
    _alloter(alloter) {
    uint32_t block_size = alloter->GetBlockLength();
    uint32_t list_size = max_size / block_size + 1;
    _buffers_cycle_list.reserve(list_size);
}

BufferSortChains::~BufferSortChains() {

}

uint32_t BufferSortChains::MoveReadPt(int32_t len) {
    uint32_t size = _sort_segment.MaxSortLength();
    size = size > len ? len : size;
    _sort_segment.Remove(size);
    return size;
}

uint32_t BufferSortChains::Read(uint8_t* data, uint32_t len) {
    uint32_t size = _sort_segment.MaxSortLength();
    size = size > len ? len : size;
    _sort_segment.Remove(size);
    return size;
}

uint32_t BufferSortChains::GetDataLength() {
    return _sort_segment.MaxSortLength();
}

std::shared_ptr<BufferBlock> BufferSortChains::GetReadBuffers() {
    return nullptr;
}

uint32_t BufferSortChains::Write(uint64_t offset, uint8_t* data, uint32_t len) {

}

uint32_t BufferSortChains::Write(uint8_t* data, uint32_t len) {
    return 0;
}

uint32_t BufferSortChains::GetFreeLength() {
    return 0;
}

uint32_t BufferSortChains::MoveWritePt(int32_t len) {
    return 0;
}

std::shared_ptr<BufferBlock> BufferSortChains::GetWriteBuffers(uint32_t len) {
    return nullptr;
}

int16_t BufferSortChains::Next(int16_t index) {
    index++;
    if (index >= _buffers_cycle_list.size()) {
        index = 0;
    }
    return index;
}

int16_t BufferSortChains::Prev(int16_t index) {
    index--;
    if (index < 0) {
        index = _buffers_cycle_list.size() - 1;
    }
    return index;
}

}
