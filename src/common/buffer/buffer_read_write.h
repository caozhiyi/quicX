// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_BUFFER_READ_WRITE
#define COMMON_BUFFER_BUFFER_READ_WRITE

#include "common/buffer/buffer_interface.h"

namespace quicx {

class BlockMemoryPool;
// read write buffer
class BufferReadWrite:
    public IBufferRead,
    public IBufferWrite {
public:
    BufferReadWrite(std::shared_ptr<BlockMemoryPool>& alloter);
    virtual ~BufferReadWrite();

    // read to data buf but don't change the read point
    // return the length of the data actually read
    virtual uint32_t ReadNotMovePt(const uint8_t* data, uint32_t len);
    // move read point
    // return the length of the data actually move
    virtual uint32_t MoveReadPt(int32_t len);
    // return the length of the data actually read
    virtual uint32_t Read(uint8_t* data, uint32_t len);
    // return remaining length of readable data
    virtual uint32_t GetDataLength();
    // return the start and end positions of readable data
    virtual std::pair<uint8_t*, uint8_t*> GetReadPair();
    // get a write buffer view
    virtual BufferReadView GetReadView(uint32_t offset = 0);
    // get a write buffer view shared ptr
    virtual std::shared_ptr<IBufferRead> GetReadViewPtr(uint32_t offset = 0);
    // get src data pos
    virtual uint8_t* GetData();

    // return the length of the actual write
    virtual uint32_t Write(const uint8_t* data, uint32_t len);
    // return the remaining length that can be written
    virtual uint32_t GetFreeLength();
    // return the length of the data actually move
    virtual uint32_t MoveWritePt(int32_t len);
    // return buffer write and end pos
    virtual std::pair<uint8_t*, uint8_t*> GetWritePair();
    // get a read buffer view
    virtual BufferWriteView GetWriteView(uint32_t offset = 0);
    // get a read buffer view shared ptr
    virtual std::shared_ptr<IBufferWrite> GetWriteViewPtr(uint32_t offset = 0);

private:
    uint32_t InnerRead(uint8_t* data, uint32_t len, bool move_pt);
    uint32_t InnerWrite(uint8_t* data, uint32_t len);
    void Clear();

private:
    uint8_t* _read_pos;             //read position
    uint8_t* _write_pos;            //write position
    bool     _can_read;             //when _read == _write? Is there any data can be read.
    uint8_t* _buffer_start;
    uint8_t* _buffer_end;
    std::weak_ptr<BlockMemoryPool> _alloter;
};

}

#endif