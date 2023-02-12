// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_BUFFER_WRITE_INTERFACE
#define COMMON_BUFFER_BUFFER_WRITE_INTERFACE

#include <memory>

namespace quicx {

// write only buffer interface
class BufferWriteView;
class IBufferWrite {
public:
    IBufferWrite() {}
    virtual ~IBufferWrite() {}
    // return the length of the actual write
    virtual uint32_t Write(const uint8_t* data, uint32_t len) = 0;
    // return the remaining length that can be written
    virtual uint32_t GetFreeLength() = 0;
    // return the length of the data actually move
    virtual uint32_t MoveWritePt(int32_t len) = 0;
    // return buffer write and end pos
    virtual std::pair<uint8_t*, uint8_t*> GetWritePair() = 0;
    // get a read buffer view
    virtual BufferWriteView GetWriteView(uint32_t offset = 0) = 0;
    // get a read buffer view shared ptr
    virtual std::shared_ptr<IBufferWrite> GetWriteViewPtr(uint32_t offset = 0) = 0;
};

}

#endif