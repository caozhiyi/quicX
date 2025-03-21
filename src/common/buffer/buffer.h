// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_BUFFER_READ_WRITE
#define COMMON_BUFFER_BUFFER_READ_WRITE

#include "common/buffer/buffer_span.h"
#include "common/buffer/if_buffer.h"

namespace quicx {
namespace common {

class BlockMemoryPool;
// read write buffer
class Buffer:
    public IBuffer {
public:
    Buffer(BufferSpan& span);
    Buffer(uint8_t* start, uint32_t len);
    Buffer(uint8_t* start, uint8_t* end);
    Buffer(std::shared_ptr<common::BlockMemoryPool>& alloter);
    virtual ~Buffer();

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
    // return the start and end positions of readable data
    virtual BufferSpan GetReadSpan();
    // get a write buffer view
    virtual BufferReadView GetReadView(uint32_t offset = 0);
    // get a write buffer view shared ptr
    virtual std::shared_ptr<common::IBufferRead> GetReadViewPtr(uint32_t offset = 0);
    // get src data pos
    virtual uint8_t* GetData();
    // clear all data
    virtual void Clear();

    // return the length of the actual write
    virtual uint32_t Write(uint8_t* data, uint32_t len);
    // return the remaining length that can be written
    virtual uint32_t GetFreeLength();
    // return the length of the data actually move
    virtual uint32_t MoveWritePt(int32_t len);
    // return buffer write and end pos
    virtual BufferSpan GetWriteSpan();
    // get a read buffer view
    virtual BufferWriteView GetWriteView(uint32_t offset = 0);
    // get a read buffer view shared ptr
    virtual std::shared_ptr<common::IBufferWrite> GetWriteViewPtr(uint32_t offset = 0);

private:
    uint32_t InnerRead(uint8_t* data, uint32_t len, bool move_pt);
    uint32_t InnerWrite(uint8_t* data, uint32_t len);

private:
    uint8_t* read_pos_;             //read position
    uint8_t* write_pos_;            //write position
    bool     can_read_;             //when read_ == write_? Is there any data can be read.
    uint8_t* buffer_start_;
    uint8_t* buffer_end_;
};

}
}

#endif