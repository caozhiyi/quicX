// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#include <cstring>
#include <assert.h>
#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_readonly.h"

namespace quicx {

BufferReadOnly::BufferReadOnly(std::shared_ptr<BlockMemoryPool>& alloter):
    _init(false),
    _alloter(alloter) {

    _buffer_start = (char*)alloter->PoolLargeMalloc();
    _buffer_end = _buffer_start + alloter->GetBlockLength();
    _read = _buffer_start;
}

BufferReadOnly::~BufferReadOnly() {
    if (_buffer_start) {
        auto alloter = _alloter.lock();
        if (alloter) {
            void* m = (void*)_buffer_start;
            alloter->PoolLargeFree(m);
        }
    }
}

uint32_t BufferReadOnly::ReadNotMovePt(char* res, uint32_t len) {
    return _Read(res, len, false);
}

uint32_t BufferReadOnly::MoveReadPt(uint32_t len) {
    /*s-----------r-------------------e*/
    if (_read <= _buffer_end) {
        size_t size = _buffer_end - _read;
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
    /*s-----------r-------------------e*/
    if (_read <= _buffer_end) {
        return uint32_t(_buffer_end - _read);

    } else {
        // shouldn't be here
        abort();
        return 0;
    }
}

std::pair<char*, char*> BufferReadOnly::GetReadPair() {
    return std::make_pair(_read, _buffer_end);
}

uint32_t BufferReadOnly::MoveWritePt(uint32_t len) {
    if (_init) {
        // shouldn't be here
        abort();
        return 0;
    }
    _init = true;

    uint32_t remain_size = uint32_t(_buffer_end - _buffer_start);
    if (len < remain_size) {
        _buffer_end = _buffer_start + len;
        return len;
    }

    return remain_size;
}

std::pair<char*, char*> BufferReadOnly::GetWritePair() {
    return std::make_pair(_buffer_start, _buffer_end);
}

uint32_t BufferReadOnly::_Read(char* res, uint32_t len, bool move_pt) {
    /*s-----------r-----w-------------e*/
    if (_read <= _buffer_end) {
        size_t size = _buffer_end - _read;
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