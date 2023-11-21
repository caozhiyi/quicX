// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_BUFFER_READ_INTERFACE
#define COMMON_BUFFER_BUFFER_READ_INTERFACE

#include <memory>
#include "common/buffer/buffer_span.h"

namespace quicx {
namespace common {

// read only buffer interface
class BufferReadView;
class IBufferRead {
public:
    IBufferRead() {}
    virtual ~IBufferRead() {}
    // read to data buf but don't change the read point
    // return the length of the data actually read
    virtual uint32_t ReadNotMovePt(uint8_t* data, uint32_t len) = 0;
    // move read point
    // return the length of the data actually move
    virtual uint32_t MoveReadPt(int32_t len) = 0;
    // return the length of the data actually read
    virtual uint32_t Read(uint8_t* data, uint32_t len) = 0;
    // return remaining length of readable data
    virtual uint32_t GetDataLength() = 0;
    // return the start and end positions of readable data
    virtual BufferSpan GetReadSpan() = 0;
    // get a write buffer view
    virtual BufferReadView GetReadView(uint32_t offset = 0) = 0;
    // get a write buffer view shared ptr
    virtual std::shared_ptr<common::IBufferRead> GetReadViewPtr(uint32_t offset = 0) = 0;
    // get src data pos
    virtual uint8_t* GetData() = 0;
    // clear all data
    virtual void Clear() = 0;
};

}
}

#endif