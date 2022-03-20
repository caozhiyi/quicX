// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_BUFFER_WRITEONLY
#define COMMON_BUFFER_BUFFER_WRITEONLY

#include <memory>
#include "common/buffer/buffer_interface.h"

namespace quicx {

class BlockMemoryPool;
// Single block cyclic cache
class BufferWriteOnly: 
    public IBufferWriteOnly {

public:
    BufferWriteOnly(std::shared_ptr<BlockMemoryPool>& alloter);
    ~BufferWriteOnly();
    // return the length of the actual write
    virtual uint32_t Write(const char* data, uint32_t len);
    // return the remaining length that can be written
    virtual uint32_t GetCanWriteLength();
    // return start and end positions of all written data
    virtual std::pair<char*, char*> GetAllData();
    // move write point
    // return the length of the data actually move
    virtual uint32_t MoveWritePt(uint32_t len);
    // return buffer write and end pos
    virtual std::pair<char*, char*> GetWritePair();

private:
    char*    _write; // write position
    char*    _buffer_start;
    char*    _buffer_end;
    std::weak_ptr<BlockMemoryPool> _alloter;
};

}

#endif