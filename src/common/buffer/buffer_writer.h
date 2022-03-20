// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_WRITER
#define COMMON_BUFFER_WRITER
#include <memory>

namespace quicx {

class BlockMemoryPool;
// buffer data writer
class BufferWriter {
public:
    BufferWriter(std::shared_ptr<BlockMemoryPool> IAlloter);
    virtual ~BufferWriter();

    // move write point
    // return left can write size
    virtual int32_t MoveWritePt(int32_t len);
    // return buffer write and end pos
    virtual std::pair<char*, char*> GetWritePair();

protected:
    uint32_t _total_size;       // total buffer size
    char*    _read;             // read position
    char*    _write;            // write position
    char*    _buffer_start;
    char*    _buffer_end;
    std::weak_ptr<BlockMemoryPool> _alloter;
};

}

#endif