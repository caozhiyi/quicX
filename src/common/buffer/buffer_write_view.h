// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_BUFFER_WRITE_VIEW
#define COMMON_BUFFER_BUFFER_WRITE_VIEW

#include "common/buffer/buffer_interface.h"

namespace quicx {
namespace common {

// write only buffer view
class BufferWriteView:
    public IBufferWrite {
public:
    BufferWriteView(uint8_t* start, uint8_t* end);
    virtual ~BufferWriteView();
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

protected:
    uint8_t* write_pos_;
    uint8_t* buffer_start_;
    uint8_t* buffer_end_;
};

}
}

#endif