// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_BUFFER_SORT_CHAINS
#define COMMON_BUFFER_BUFFER_SORT_CHAINS

#include <map>
#include "common/buffer/buffer_block.h"
#include "common/buffer/buffer_chains.h"

namespace quicx {

class BufferSortChains:
    public BufferChains {
public:
    BufferSortChains(std::shared_ptr<BlockMemoryPool>& alloter);
    virtual ~BufferSortChains();

    // move read point
    // return the length of the data actually move
    virtual uint32_t MoveReadPt(int32_t len);
    // return the length of the data actually read
    virtual uint32_t Read(uint8_t* data, uint32_t len);
    // return remaining length of readable data
    virtual uint32_t GetDataLength();
    // return readable buffer list
    virtual std::shared_ptr<BufferBlock> GetReadBuffers();

    // return the length of the actual write
    virtual uint32_t Write(uint8_t* data, uint32_t len);
    // return the remaining length that can be written
    virtual uint32_t GetFreeLength();
    // return the length of the data actually move
    virtual uint32_t MoveWritePt(int32_t len);
    // return buffer write list
    virtual std::shared_ptr<BufferBlock> GetWriteBuffers(uint32_t len);

protected:
    enum SegmentType {
        ST_END = 0,
        ST_START = 1,
    };
    std::map<uint64_t, SegmentType> _segment_map;
};

}

#endif