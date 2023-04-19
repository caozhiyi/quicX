#include "common/buffer/buffer_sort_chains.h"

namespace quicx {

bool SortSegment::Insert(uint64_t index, uint32_t len) {
    auto iter = _segment_map.lower_bound(index);
    if (iter != _segment_map.end() && iter->second == ST_START) {
        return false;
    }
    
    _segment_map[index] = ST_START;
    _segment_map[index + len] = ST_END;
    return true;
}

bool SortSegment::Remove(uint32_t len) {
    auto iter = _segment_map.begin();
    if (iter == _segment_map.end()) {
        return false;
    }
    
    uint64_t start_offset = iter->first + len;
    while (iter != _segment_map.end()) {
        if (iter->first < start_offset) {
            iter = _segment_map.erase(iter);
        
        } else {
            break;
        }
    }

    if (iter == _segment_map.end()) {
        return false;
    }
    _segment_map[start_offset] = ST_START;
    return true;
}

uint64_t SortSegment::MaxSortLength() {
    auto iter = _segment_map.begin();
    if (iter == _segment_map.end()) {
        return 0;
    }

    uint64_t start_offset = iter->first;
    for (;iter != _segment_map.end(); iter++) {
        if (iter->second == ST_END) {
            break;
        }
    }
    if (iter == _segment_map.end()) {
        return 0;
    }
    return iter->first - start_offset;
}

BufferSortChains::BufferSortChains(std::shared_ptr<BlockMemoryPool>& alloter, uint32_t max_size) {

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

}
