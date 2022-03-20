// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_BUFFER_INTERFACE
#define COMMON_BUFFER_BUFFER_INTERFACE

#include <memory>

namespace quicx {

// buffer data writer
class IBufferWriter {
public:
    IBufferWriter() {}
    virtual ~IBufferWriter() {}

    // move write point
    // return left can write size
    virtual int32_t MoveWritePt(int32_t len) = 0;
    // return buffer write and end pos
    virtual std::pair<char*, char*> GetWritePair() = 0;
};

// read only buffer interface
class IBufferReadOnly {
public:
    IBufferReadOnly() {}
    virtual ~IBufferReadOnly() {}

    // read to res buf but don't change the read point
    // return read size
    virtual uint32_t ReadNotMovePt(char* res, uint32_t len) = 0;
    // move read point
    virtual uint32_t MoveReadPt(uint32_t len) = 0;

    virtual uint32_t Read(char* res, uint32_t len) = 0;
    
    virtual uint32_t GetCanReadLength() = 0;
};

// read only buffer interface
class IBufferWriteOnly {
public:
    IBufferWriteOnly() {}
    virtual ~IBufferWriteOnly() {}

    virtual uint32_t Write(const char* data, uint32_t len) = 0;

    virtual uint32_t GetCanWriteLength() = 0;
};

}

#endif