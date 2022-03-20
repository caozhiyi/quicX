// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#include <cstring>
#include <assert.h>
#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_readonly.h"

namespace quicx {

BufferReadOnly::BufferReadOnly(std::shared_ptr<BlockMemoryPool>& IAlloter):
    BufferWriter(IAlloter) {

}

BufferReadOnly::~BufferReadOnly() {

}

uint32_t BufferReadOnly::ReadNotMovePt(char* res, uint32_t len) {
    return _Read(res, len, false);
}

uint32_t BufferReadOnly::MoveReadPt(uint32_t len) {
    /*s-----------r-----w-------------e*/
    if (_read <= _write) {
        size_t size = _write - _read;
        // res can load all
        if (size <= len) {
            _read += size;
            return (uint32_t)size;

        // only read len
        } else {
            _read += len;
            return len;
        }

    } else {
        // shouldn't be here
        abort();
        return 0;
    }
}

uint32_t BufferReadOnly::Read(char* res, uint32_t len) {
    if (res == nullptr) {
        return 0;
    }
    return _Read(res, len, true);
}

uint32_t BufferReadOnly::GetCanReadLength() {
    /*s-----------r-----w-------------e*/
    if (_read <= _write) {
        return uint32_t(_write - _read);

    } else {
        // shouldn't be here
        abort();
        return 0;
    }
}

uint32_t BufferReadOnly::_Read(char* res, uint32_t len, bool move_pt) {
    /*s-----------r-----w-------------e*/
    if (_read <= _write) {
        size_t size = _write - _read;
        // res can load all
        if (size <= len) {
            memcpy(res, _read, size);
            if(move_pt) {
                _read += size;
            }
            return (uint32_t)size;

        // only read len
        } else {
            memcpy(res, _read, len);
            if(move_pt) {
                _read += len;
            }
            return len;
        }

    } else {
        // shouldn't be here
        abort();
        return 0;
    }
}

}