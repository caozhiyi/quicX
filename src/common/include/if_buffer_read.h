// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_IF_BUFFER_READ
#define COMMON_BUFFER_IF_BUFFER_READ

#include <cstdint>
#include <functional>

namespace quicx {

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
    // visit readable segments without modifying state
    virtual void VisitData(const std::function<void(uint8_t*, uint32_t)>& visitor) = 0;
    // return remaining length of readable data
    virtual uint32_t GetDataLength() = 0;
    // clear all data
    virtual void Clear() = 0;
};

}

#endif