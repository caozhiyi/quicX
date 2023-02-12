// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_BUFFER_CHAINS_INTERFACE
#define COMMON_BUFFER_BUFFER_CHAINS_INTERFACE

#include <memory>
#include "common/buffer/buffer_block.h"
#include "common/buffer/buffer_interface.h"

namespace quicx {

// buffer chains interface
class IBufferChains {
public:
    IBufferChains() {}
    virtual ~IBufferChains() {}

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
    // return readable buffer list
    virtual std::shared_ptr<BufferBlock> GetReadBuffers() = 0;

    // return the length of the actual write
    virtual uint32_t Write(uint8_t* data, uint32_t len) = 0;
    // return the remaining length that can be written
    virtual uint32_t GetFreeLength() = 0;
    // return the length of the data actually move
    virtual uint32_t MoveWritePt(int32_t len) = 0;
    // return buffer write list
    virtual std::shared_ptr<BufferBlock> GetWriteBuffers(uint32_t len) = 0;
};

}

#endif