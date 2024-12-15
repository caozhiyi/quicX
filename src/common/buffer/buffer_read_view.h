// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_BUFFER_READ_VIEW
#define COMMON_BUFFER_BUFFER_READ_VIEW

#include "common/buffer/buffer_read_interface.h"

namespace quicx {
namespace common {

class BlockMemoryPool;
// read only buffer
class BufferReadView:
    public IBufferRead {
public:
    BufferReadView(BufferSpan span);
    BufferReadView(uint8_t* start, uint32_t len);
    BufferReadView(uint8_t* start, uint8_t* end);
    virtual ~BufferReadView();
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
    virtual void Clear() {}

protected:
    uint32_t Read(uint8_t* data, uint32_t len, bool move_pt);

protected:
    uint8_t* read_pos_;
    uint8_t* buffer_start_;
    uint8_t* buffer_end_;
};

}
}

#endif