#include "common/buffer/buffer_sort_chains.h"

namespace quicx {

bool SortSegment::Insert(uint64_t offset, uint32_t len) {
    if (offset <= _start_offset && _start_offset != 0) {
        return false;
    }

    if (offset >= _limit_offset) {
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
    uint64_t max_len = ContinuousLength();
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
            remove_size = iter->first - _start_offset;
            iter = _segment_map.erase(iter);
        
        } else {
            break;
        }
    }

    _start_offset = start_offset;
    if (iter != _segment_map.end()) {
        _segment_map[start_offset] = ST_START;
    }
    return max_len;
}

uint64_t SortSegment::ContinuousLength() {
    auto iter = _segment_map.begin();
    for (;iter != _segment_map.end(); iter++) {
        if (iter->second == ST_END) {
            break;
        }
    }
    if (iter == _segment_map.end()) {
        return 0;
    }
    return iter->first - _start_offset;
}

bool SortSegment::UpdateLimitOffset(uint64_t offset) {
    if (_limit_offset < offset) {
        _limit_offset = offset;
        return true;
    }
    return false;
}

BufferSortChains::BufferSortChains(std::shared_ptr<BlockMemoryPool>& alloter):
    _cur_write_offset(0),
    _read_pos(nullptr),
    _write_pos(nullptr),
    _alloter(alloter) {

}

BufferSortChains::~BufferSortChains() {

}

bool BufferSortChains::UpdateOffset(uint64_t offset) {
    return _sort_segment.UpdateLimitOffset(offset);
}

uint32_t BufferSortChains::ReadNotMovePt(uint8_t* data, uint32_t len) {
    if (len == 0 || data == nullptr) {
        return 0;
    }
    
    uint32_t read_len = _sort_segment.ContinuousLength();
    read_len = read_len > len ? len : read_len;

    uint32_t size = 0;
    for (auto iter = _read_pos; iter && iter->GetDataLength() > 0; iter = iter->GetNext()) {
        size += iter->ReadNotMovePt(data + size, len - size);
        if (size >= read_len) {
            break;
        }
    }
    return size;
}

uint32_t BufferSortChains::MoveReadPt(int32_t len) {
    if (len == 0) {
        return 0;
    }
    
    uint32_t read_len = _sort_segment.ContinuousLength();
    read_len = read_len > len ? len : read_len;

    uint32_t size = 0;
    for (; _read_pos && _read_pos->GetDataLength() > 0; _read_pos = _read_pos->GetNext()) {
        size += _read_pos->MoveReadPt(len - size);
        if (size >= read_len) {
            break;
        }
        _buffer_list.PopFront();
    }
    _sort_segment.Remove(size);

    return size;
}

uint32_t BufferSortChains::Read(uint8_t* data, uint32_t len) {
    if (len == 0 || data == nullptr) {
        return 0;
    }

    uint32_t read_len = _sort_segment.ContinuousLength();
    read_len = read_len > len ? len : read_len;

    uint32_t size = 0;
    for (; _read_pos && _read_pos->GetDataLength() > 0; _read_pos = _read_pos->GetNext()) {
        size += _read_pos->Read(data + size, len - size);
        if (size >= read_len) {
            break;
        }
        _buffer_list.PopFront();
    }
    _sort_segment.Remove(size);
    return size;
}

uint32_t BufferSortChains::GetDataLength() {
    return _sort_segment.ContinuousLength();
}

uint32_t BufferSortChains::Write(uint64_t offset, uint8_t* data, uint32_t len) {
    if(!_sort_segment.Insert(offset, len)) {
        return 0;
    }
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
}

}
