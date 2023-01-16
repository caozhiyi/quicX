// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_BUFFER_CHAINS
#define COMMON_BUFFER_BUFFER_CHAINS

#include <list>
#include <functional>
#include "common/buffer/buffer_chains_interface.h"

namespace quicx {

class BufferReadWrite;
class BufferChains:
    public IBufferChains {
public:
    BufferChains(std::shared_ptr<BlockMemoryPool>& alloter);
    virtual ~BufferChains();

    // read to data buf but don't change the read point
    // return the length of the data actually read
    virtual uint32_t ReadNotMovePt(const uint8_t* data, uint32_t len);
    // move read point
    // return the length of the data actually move
    virtual uint32_t MoveReadPt(int32_t len);
    // return the length of the data actually read
    virtual uint32_t Read(const uint8_t* data, uint32_t len);
    // return remaining length of readable data
    virtual uint32_t GetDataLength();
    // return readable buffer list
    virtual std::vector<std::shared_ptr<IBufferRead>> GetReadBuffers();

    // return the length of the actual write
    virtual uint32_t Write(const uint8_t* data, uint32_t len);
    // return the remaining length that can be written
    virtual uint32_t GetFreeLength();
    // return the length of the data actually move
    virtual uint32_t MoveWritePt(int32_t len);
    // return buffer write list
    virtual std::vector<std::shared_ptr<IBufferWrite>> GetWriteBuffers(uint32_t len = 0);

private:
    typedef std::function<uint32_t(std::shared_ptr<BufferReadWrite>)> BufferOperation;
    uint32_t InnerRead(BufferOperation op);
    uint32_t InnerWrite(BufferOperation op);
    void Clear();

private:
    std::list<std::shared_ptr<BufferReadWrite>>::iterator _read_pos;
    std::list<std::shared_ptr<BufferReadWrite>>::iterator _write_pos;
    
    std::weak_ptr<BlockMemoryPool> _alloter;
    std::list<std::shared_ptr<BufferReadWrite>> _buffer_list;
};

}

#endif