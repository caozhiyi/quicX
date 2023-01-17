// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_BUFFER_BLOCK
#define COMMON_BUFFER_BUFFER_BLOCK

#include <memory>
#include "common/structure/linked_list.h"
#include "common/buffer/buffer_read_write.h"

namespace quicx {

class BlockMemoryPool;
class BufferBlock:
    public BufferReadWrite,
    public LinkedListSolt<BufferBlock> {
public:
    BufferBlock(std::shared_ptr<BlockMemoryPool>& alloter): BufferReadWrite(alloter) {}
    ~BufferBlock() {}
};

}

#endif