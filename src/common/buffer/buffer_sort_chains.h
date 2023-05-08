// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_BUFFER_SORT_CHAINS
#define COMMON_BUFFER_BUFFER_SORT_CHAINS

#include <map>
#include "common/buffer/buffer_chains.h"

namespace quicx {

class BufferSortChains {
public:
    BufferSortChains(std::shared_ptr<BlockMemoryPool>& alloter);
    ~BufferSortChains();
    // read to data buf but don't change the read point
    // return the length of the data actually read
    uint32_t ReadNotMovePt(uint8_t* data, uint32_t len);
    // move read point
    // return the length of the data actually move
    uint32_t MoveReadPt(uint32_t len);
    // return the length of the data actually move
    uint32_t MoveWritePt(int32_t len);
    // return the length of the data actually read
    uint32_t Read(uint8_t* data, uint32_t len);
    // return the length of the actual write
    uint32_t Write(uint8_t* data, uint32_t len);

protected:
    uint64_t _cur_write_offset;
    std::shared_ptr<BufferBlock> _read_pos;
    std::shared_ptr<BufferBlock> _write_pos;
    
    LinkedList<BufferBlock> _buffer_list;
    std::shared_ptr<BlockMemoryPool> _alloter;
};

}

#endif