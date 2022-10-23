// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_BUFFER_INTERFACE
#define COMMON_BUFFER_BUFFER_INTERFACE

#include <memory>

namespace quicx {

// read only buffer interface
class IBufferReadOnly {
public:
    IBufferReadOnly() {}
    virtual ~IBufferReadOnly() {}
    // read to res buf but don't change the read point
    // return the length of the data actually read
    virtual uint32_t ReadNotMovePt(uint8_t* res, uint32_t len) = 0;
    // move read point
    // return the length of the data actually move
    virtual uint32_t MoveReadPt(uint32_t len) = 0;
    // return the length of the data actually read
    virtual uint32_t Read(uint8_t* res, uint32_t len) = 0;
    // return remaining length of readable data
    virtual uint32_t GetCanReadLength() = 0;
    // return the start and end positions of readable data
    virtual std::pair<uint8_t*, uint8_t*> GetReadPair() = 0;

    // move write point
    // return the length of the data actually move
    virtual uint32_t MoveWritePt(uint32_t len) = 0;
    // return buffer write and end pos
    virtual std::pair<uint8_t*, uint8_t*> GetWritePair() = 0;
};

// write only buffer interface
class IBufferWriteOnly {
public:
    IBufferWriteOnly() {}
    virtual ~IBufferWriteOnly() {}
    // return the length of the actual write
    virtual uint32_t Write(const uint8_t* data, uint32_t len) = 0;
    // return the remaining length that can be written
    virtual uint32_t GetCanWriteLength() = 0;
    // return start and end positions of all written data
    virtual std::pair<uint8_t*, uint8_t*> GetAllData() = 0;
    // move write point
    // return the length of the data actually move
    virtual uint32_t MoveWritePt(uint32_t len) = 0;
    // return buffer write and end pos
    virtual std::pair<uint8_t*, uint8_t*> GetWritePair() = 0;
};

}

#endif