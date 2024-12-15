// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_BUFFER_CHAINS
#define COMMON_BUFFER_BUFFER_CHAINS

#include "common/buffer/buffer_block.h"
#include "common/buffer/if_buffer_chains.h"

namespace quicx {
namespace common {

class BufferChains:
    public IBufferChains {
public:
    BufferChains(std::shared_ptr<common::BlockMemoryPool>& alloter);
    virtual ~BufferChains();

    // read to data buf but don't change the read point
    // return the length of the data actually read
    virtual uint32_t ReadNotMovePt(uint8_t* data, uint32_t len);
    // move read point
    // return the length of the data actually move
    virtual uint32_t MoveReadPt(int32_t len);
    // return the length of the data actually read
    virtual uint32_t Read(uint8_t* data, uint32_t len);
    // return remaining length of readable data
    virtual uint32_t GetDataLength();
    // return readable buffer list
    virtual std::shared_ptr<BufferBlock> GetReadBuffers();

    // return the length of the actual write
    virtual uint32_t Write(uint8_t* data, uint32_t len);
    // return the remaining length that can be written
    virtual uint32_t GetFreeLength();
    // return the length of the data actually move
    virtual uint32_t MoveWritePt(int32_t len);
    // return buffer write list
    virtual std::shared_ptr<BufferBlock> GetWriteBuffers(uint32_t len);

protected:
    void Clear();

protected:
    std::shared_ptr<BufferBlock> read_pos_;
    std::shared_ptr<BufferBlock> write_pos_;
    
    LinkedList<BufferBlock> buffer_list_;
    std::shared_ptr<common::BlockMemoryPool> alloter_;
};

}
}

#endif