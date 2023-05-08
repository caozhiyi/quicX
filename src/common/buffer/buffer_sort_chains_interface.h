// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_BUFFER_SORT_CHAINS_INTERFACE
#define COMMON_BUFFER_BUFFER_SORT_CHAINS_INTERFACE

#include <stdint.h>

namespace quicx {

class IBufferSortChains {
public:
    IBufferSortChains() {}
    virtual ~IBufferSortChains() {}

    // read to data buf but don't change the read point
    // return the length of the data actually read
    virtual uint32_t ReadNotMovePt(uint8_t* data, uint32_t len) = 0;
    // move read point
    // return the length of the data actually move
    virtual uint32_t MoveReadPt(uint32_t len) = 0;
    // move write point
    // return the length of the data actually move
    virtual uint32_t MoveWritePt(int32_t len) = 0;
    // return the length of the data actually read
    virtual uint32_t Read(uint8_t* data, uint32_t len) = 0;
    // return the length of the actual write
    virtual uint32_t Write(uint8_t* data, uint32_t len) = 0;
};

}

#endif