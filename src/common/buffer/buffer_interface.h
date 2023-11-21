// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_BUFFER_INTERFACE
#define COMMON_BUFFER_BUFFER_INTERFACE

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_read_interface.h"
#include "common/buffer/buffer_write_interface.h"

namespace quicx {
namespace common {

class IBuffer:
    public IBufferRead,
    public IBufferWrite {
public:
    IBuffer(const std::shared_ptr<common::BlockMemoryPool>& alloter): _alloter(alloter) {}
    virtual ~IBuffer() {}

protected:
    std::weak_ptr<BlockMemoryPool> _alloter;
};

}
}

#endif