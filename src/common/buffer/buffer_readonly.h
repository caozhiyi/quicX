// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_BUFFER_READONLY
#define COMMON_BUFFER_BUFFER_READONLY

#include <memory>
#include "common/buffer/buffer_interface.h"

namespace quicx {

class BlockMemoryPool;
// Single block cyclic cache
class BufferReadOnly: 
    public IBufferReadOnly {

public:
    BufferReadOnly(std::shared_ptr<BlockMemoryPool>& alloter);
    ~BufferReadOnly();

    // read to res buf but don't change the read point
    // return the length of the data actually read
    virtual uint32_t ReadNotMovePt(char* res, uint32_t len);
    // move read point
    // return the length of the data actually move
    virtual uint32_t MoveReadPt(uint32_t len);
    // return the length of the data actually read
    virtual uint32_t Read(char* res, uint32_t len);
    // return remaining length of readable data
    virtual uint32_t GetCanReadLength();
    // return the start and end positions of readable data
    virtual std::pair<char*, char*> GetReadPair();

    // initialization interfaces, which can only be called once
    // move write point
    // return the length of the data actually move
    virtual uint32_t MoveWritePt(uint32_t len);
    // initialization interfaces, which can only be called once
    // return buffer write and end pos
    virtual std::pair<char*, char*> GetWritePair();

protected:
    uint32_t _Read(char* res, uint32_t len, bool move_pt);

private:
    char* _read; // read position
    char* _buffer_start;
    char* _buffer_end;
    bool  _init;
    std::weak_ptr<BlockMemoryPool> _alloter;
};

}

#endif