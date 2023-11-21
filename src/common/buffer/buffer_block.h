// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_BUFFER_BLOCK
#define COMMON_BUFFER_BUFFER_BLOCK

#include <memory>
#include "common/buffer/buffer.h"
#include "common/structure/linked_list.h"

namespace quicx {
namespace common {

class BlockMemoryPool;
class BufferBlock:
    public Buffer,
    public LinkedListSolt<BufferBlock> {
public:
    BufferBlock(std::shared_ptr<common::BlockMemoryPool>& alloter): Buffer(alloter) {}
    ~BufferBlock() {}
};

}
}

#endif