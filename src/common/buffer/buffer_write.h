// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_BUFFER_WRITE
#define COMMON_BUFFER_BUFFER_WRITE

#include "common/buffer/buffer_write_view.h"

namespace quicx {

class BlockMemoryPool;
// write only buffer
class BufferWrite:
    public BufferWriteView {
public:
    BufferWrite(uint8_t* data, uint8_t* end, std::shared_ptr<BlockMemoryPool>& alloter);
    virtual ~BufferWrite();

private:
    std::weak_ptr<BlockMemoryPool> _alloter;
};

}

#endif