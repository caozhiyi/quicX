// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_BUFFER_INTERFACE
#define COMMON_BUFFER_BUFFER_INTERFACE

#include <memory>

namespace quicx {

// read only buffer interface
class BufferReadView;
class IBufferRead {
public:
    IBufferRead() {}
    virtual ~IBufferRead() {}
    // read to data buf but don't change the read point
    // return the length of the data actually read
    virtual uint32_t ReadNotMovePt(const uint8_t* data, uint32_t len) = 0;
    // move read point
    // return the length of the data actually move
    virtual uint32_t MoveReadPt(int32_t len) = 0;
    // return the length of the data actually read
    virtual uint32_t Read(const uint8_t* data, uint32_t len) = 0;
    // return remaining length of readable data
    virtual uint32_t GetDataLength() = 0;
    // return the start and end positions of readable data
    virtual std::pair<uint8_t*, uint8_t*> GetReadPair() = 0;
    // get a write buffer view
    virtual BufferReadView GetReadView(uint32_t offset = 0) = 0;
    // get a write buffer view shared ptr
    virtual std::shared_ptr<IBufferRead> GetReadViewPtr(uint32_t offset = 0) = 0;
    // get src data pos
    virtual uint8_t* GetData() = 0;
};

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