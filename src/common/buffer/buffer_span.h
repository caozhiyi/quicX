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
    BufferSpan(): _start(nullptr), _end(nullptr) {}
    BufferSpan(uint8_t* start, uint32_t len): _start(start), _end(start + len) {}
    BufferSpan(uint8_t* start, uint8_t* end): _start(start), _end(end) {}
    virtual ~BufferSpan() {}

    uint8_t* GetStart() { return _start; }
    uint8_t* GetEnd() { return _end; }
    uint32_t GetLength() { return uint32_t(_end - _start); }

private:
    uint8_t* _start;
    uint8_t* _end;
};

}
}

#endif