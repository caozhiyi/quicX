// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_writer.h"

namespace quicx {

BufferWriter::BufferWriter(std::shared_ptr<BlockMemoryPool> IAlloter): 
    _alloter(IAlloter) {

    _buffer_start = (char*)IAlloter->PoolLargeMalloc();
    _total_size = IAlloter->GetBlockLength();
    _buffer_end = _buffer_start + _total_size;
    _read = _write = _buffer_start;
}

BufferWriter::~BufferWriter() {
    if (_buffer_start) {
        auto IAlloter = _alloter.lock();
        if (IAlloter) {
            void* m = (void*)_buffer_start;
            IAlloter->PoolLargeFree(m);
        }
    }
}

int32_t BufferWriter::MoveWritePt(int32_t len) {
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
    return (int32_t)(_buffer_end - _write);
}

std::pair<char*, char*> BufferWriter::GetWritePair() {
    return std::make_pair(_write, _buffer_end);
}

}