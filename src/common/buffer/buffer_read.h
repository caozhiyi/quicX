// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_BUFFER_READ
#define COMMON_BUFFER_BUFFER_READ

#include "common/buffer/buffer_read_view.h"

namespace quicx {

class BlockMemoryPool;
// read only buffer
class BufferRead:
    public BufferReadView {
public:
    BufferRead(const uint8_t* data, const uint8_t* end, std::shared_ptr<BlockMemoryPool>& alloter);
    virtual ~BufferRead();

private:
    std::weak_ptr<BlockMemoryPool> _alloter;
};

}

#endif