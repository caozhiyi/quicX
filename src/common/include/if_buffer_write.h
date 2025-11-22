// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_IF_BUFFER_WRITE
#define COMMON_BUFFER_IF_BUFFER_WRITE

#include <cstdint>

namespace quicx {

class IBufferWrite {
public:
    IBufferWrite() {}
    virtual ~IBufferWrite() {}
    // Copy |len| bytes from |data| into the writable region and advance the
    // internal write pointer. The actual copied length is capped by the
    // remaining capacity. Returns the number of bytes written.
    virtual uint32_t Write(const uint8_t* data, uint32_t len) = 0;

    // Move the write pointer by |len| bytes without touching memory. Positive
    // values advance the pointer, negative values rewind it. The movement is
    // always clamped to the writable range and the actual distance moved is
    // returned to the caller.
    virtual uint32_t MoveWritePt(uint32_t len) = 0;
};

}

#endif