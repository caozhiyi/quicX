// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_BUFFER_INTERFACE
#define COMMON_BUFFER_BUFFER_INTERFACE

#include "common/alloter/pool_block.h"
#include "common/buffer/if_buffer_read.h"
#include "common/buffer/if_buffer_write.h"

namespace quicx {
namespace common {

class IBuffer:
    public IBufferRead,
    public IBufferWrite {
public:
    IBuffer(const std::shared_ptr<common::BlockMemoryPool>& alloter): alloter_(alloter) {}
    virtual ~IBuffer() {}

protected:
    std::weak_ptr<BlockMemoryPool> alloter_;
};

}
}

#endif