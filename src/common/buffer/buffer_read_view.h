// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_BUFFER_READ_VIEW
#define COMMON_BUFFER_BUFFER_READ_VIEW

#include "common/buffer/buffer_interface.h"

namespace quicx {

class BlockMemoryPool;
// read only buffer
class BufferReadView:
    public IBufferRead {
public:
    BufferReadView(const uint8_t* start, const uint8_t* end);
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
    virtual std::pair<const uint8_t*, const uint8_t*> GetReadPair();
    // get a write buffer view
    virtual BufferReadView GetReadView(uint32_t offset = 0);
    // get a write buffer view shared ptr
    virtual std::shared_ptr<IBufferRead> GetReadViewPtr(uint32_t offset = 0);
    // get src data pos
    virtual const uint8_t* GetData();
protected:
    uint32_t Read(uint8_t* data, uint32_t len, bool move_pt);

protected:
    const uint8_t* _read_pos;
    const uint8_t* _buffer_start;
    const uint8_t* _buffer_end;
};

}

#endif