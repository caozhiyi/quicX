// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#include <cstring>
#include <stddef.h>
#include "common/buffer/fix_sort_buffer.h"

namespace quicx {
FixSortBuffer::FixSortBuffer(uint32_t len):
    _can_read(false) {
    _buffer_start = new uint8_t[len];
    _buffer_end = _buffer_start + len;
    _read_pos = _write_pos = _buffer_start;
}

FixSortBuffer::~FixSortBuffer() {
    delete []_buffer_start;
}

uint32_t FixSortBuffer::ReadNotMovePt(uint8_t* data, uint32_t len) {

}

uint32_t FixSortBuffer::MoveReadPt(int32_t len) {
    
}

uint32_t FixSortBuffer::Read(uint8_t* data, uint32_t len) {
    
}

uint32_t FixSortBuffer::GetDataLength() {
    
}

uint32_t FixSortBuffer::Write(uint64_t offset, uint8_t* data, uint32_t len) {
    
}

uint32_t FixSortBuffer::Read(uint8_t* data, uint32_t len, bool move_pt) {
    /*s-----------r-----w-------------e*/
    if (_read_pos < _write_pos) {
        size_t size = _write_pos - _read_pos;
        // res can load all
        if (size <= len) {
            memcpy(data, _read_pos, size);
            if(move_pt) {
                Clear();
            }
            return (uint32_t)size;

        // only read len
        } else {
            memcpy(data, _read_pos, len);
            if(move_pt) {
                _read_pos += len;
            }
            return len;
        }

    /*s-----------w-----r-------------e*/
    /*s----------------wr-------------e*/
    } else {
        if(!_can_read && _read_pos == _write_pos) {
            return 0;
        }
        size_t size_start = _write_pos - _buffer_start;
        size_t size_end = _buffer_end - _read_pos;
        size_t size =  size_start + size_end;
        // res can load all
        if (size <= len) {
            memcpy(data, _read_pos, size_end);
            memcpy(data + size_end, _buffer_start, size_start);
            if(move_pt) {
                // reset point
                Clear();
            }
            return (uint32_t)size;

        } else {
            if (len <= size_end) {
                memcpy(data, _read_pos, len);
                if(move_pt) {
                    _read_pos += len;
                }
                return len;

            } else {
                size_t left = len - size_end;
                memcpy(data, _read_pos, size_end);
                memcpy(data + size_end, _buffer_start, left);
                if(move_pt) {
                    _read_pos = _buffer_start + left;
                }
                return len;
            }
        }
    }
}

void FixSortBuffer::Clear() {
    _write_pos = _read_pos = _buffer_start;
    _can_read = false;
}

}
