// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_IF_BUFFER_WRITE
#define COMMON_BUFFER_IF_BUFFER_WRITE

#include <memory>
#include "common/buffer/buffer_span.h"

namespace quicx {
namespace common {

// write only buffer interface
class BufferWriteView;
class IBufferWrite {
public:
    IBufferWrite() {}
    virtual ~IBufferWrite() {}
    // return the length of the actual write
    virtual uint32_t Write(uint8_t* data, uint32_t len) = 0;
    // return the remaining length that can be written
    virtual uint32_t GetFreeLength() = 0;
    // return the length of the data actually move
    virtual uint32_t MoveWritePt(int32_t len) = 0;
    // return buffer write and end pos
    virtual BufferSpan GetWriteSpan() = 0;
    // get a read buffer view
    virtual BufferWriteView GetWriteView(uint32_t offset = 0) = 0;
    // get a read buffer view shared ptr
    virtual std::shared_ptr<common::IBufferWrite> GetWriteViewPtr(uint32_t offset = 0) = 0;
};

}
}

#endif