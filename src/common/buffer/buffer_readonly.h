// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_BUFFER_READONLY
#define COMMON_BUFFER_BUFFER_READONLY

#include <memory>
#include "common/buffer/buffer_writer.h"
#include "common/buffer/buffer_interface.h"

namespace quicx {

class BlockMemoryPool;
// Single block cyclic cache
class BufferReadOnly: 
    public IBufferReadOnly,
    public BufferWriter {

public:
    BufferReadOnly(std::shared_ptr<BlockMemoryPool>& IAlloter);
    ~BufferReadOnly();

    // read to res buf but don't change the read point
    // return read size
    virtual uint32_t ReadNotMovePt(char* res, uint32_t len);
    // move read point
    virtual uint32_t MoveReadPt(uint32_t len);

    virtual uint32_t Read(char* res, uint32_t len);
    
    virtual uint32_t GetCanReadLength();

protected:
    uint32_t _Read(char* res, uint32_t len, bool move_pt);
};

}

#endif