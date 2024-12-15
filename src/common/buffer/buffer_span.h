// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_BUFFER_SPAN
#define COMMON_BUFFER_BUFFER_SPAN

#include <cstdint>

namespace quicx {
namespace common {

class BufferSpan {
public:
    BufferSpan(): start_(nullptr), end_(nullptr) {}
    BufferSpan(uint8_t* start, uint32_t len): start_(start), end_(start + len) {}
    BufferSpan(uint8_t* start, uint8_t* end): start_(start), end_(end) {}
    virtual ~BufferSpan() {}

    uint8_t* GetStart() { return start_; }
    uint8_t* GetEnd() { return end_; }
    uint32_t GetLength() { return uint32_t(end_ - start_); }

private:
    uint8_t* start_;
    uint8_t* end_;
};

}
}

#endif