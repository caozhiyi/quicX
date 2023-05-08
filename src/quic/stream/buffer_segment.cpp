#include "quic/stream/buffer_segment.h"

namespace quicx {

bool BufferSegment::Insert(uint64_t offset, uint32_t len) {
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

uint32_t BufferSegment::Remove(uint32_t len) {
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

uint64_t BufferSegment::ContinuousLength() {
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

bool BufferSegment::UpdateLimitOffset(uint64_t offset) {
    if (_limit_offset < offset) {
        _limit_offset = offset;
        return true;
    }
    return false;
}

}
