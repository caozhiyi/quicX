// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#include <cstring>
#include <assert.h>
#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_writeonly.h"

namespace quicx {

BufferWriteOnly::BufferWriteOnly(std::shared_ptr<BlockMemoryPool>& IAlloter):
    _alloter(IAlloter) {

    _buffer_start = (char*)IAlloter->PoolLargeMalloc();
    _buffer_end = _buffer_start + IAlloter->GetBlockLength();
    _write = _buffer_start;

}

BufferWriteOnly::~BufferWriteOnly() {
     if (_buffer_start) {
        auto IAlloter = _alloter.lock();
        if (IAlloter) {
            void* m = (void*)_buffer_start;
            IAlloter->PoolLargeFree(m);
        }
    }
}

uint32_t BufferWriteOnly::Write(const char* data, uint32_t len) {
    /*s-----------w-------------------e*/
    if (_write < _buffer_end) {
        size_t size = _buffer_end - _write;
        // can save all data
        if (len <= size) {
            memcpy(_write, data, len);
            _write += len;
            return len;

        // can save a part of data
        } else {
            memcpy(_write, data, size);
            _write += size;

            return (uint32_t)size;
        }

    /*s------------------------------we*/
    } else if (_write == _buffer_end) {
        return 0;

    } else {
        // shouldn't be here
        abort();
        return 0;
    }
}

std::pair<char*, char*> BufferWriteOnly::GetAllData() {
    return std::make_pair(_buffer_start, _write);
}

uint32_t BufferWriteOnly::GetCanWriteLength() {
    /*s-----------w-------------------e*/
    if (_write <= _buffer_end) {
        return uint32_t(_buffer_end - _write);

    } else {
        // shouldn't be here
        abort();
        return 0;
    }
}


uint32_t BufferWriteOnly::MoveWritePt(uint32_t len) {
    if (_buffer_end < _write) {
        // shouldn't be here
        abort();
    }
    
    size_t size = _buffer_end - _write;
    if (size >= len) {
        _write += len;
    } else {
        _write += size;
    }
    return (uint32_t)(_buffer_end - _write);
}

std::pair<char*, char*> BufferWriteOnly::GetWritePair() {
    return std::make_pair(_write, _buffer_end);
}

}