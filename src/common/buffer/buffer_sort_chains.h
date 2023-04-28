// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_BUFFER_SORT_CHAINS
#define COMMON_BUFFER_BUFFER_SORT_CHAINS

#include <map>
#include "common/buffer/buffer_chains.h"

namespace quicx {

class SortSegment {
public:
    SortSegment(): _start_offset(0), _max_offset(0) {}
    ~SortSegment() {}

    bool Insert(uint64_t offset, uint32_t len);
    uint32_t Remove(uint32_t len);
    uint64_t MaxSortLength();
    uint64_t GetStartOffset() { return _start_offset; }
    uint64_t GetMaxOffset() { return _max_offset; }
    bool UpdateMaxOffset(uint64_t offset);

protected:
    enum SegmentType {
        ST_END = 0,
        ST_START = 1,
    };
    uint64_t _start_offset;
    uint64_t _max_offset;
    std::map<uint64_t, SegmentType> _segment_map;
};

class BufferSortChains {
public:
    BufferSortChains(std::shared_ptr<BlockMemoryPool>& alloter);
    ~BufferSortChains();

    bool UpdateOffset(uint64_t offset);
    // read to data buf but don't change the read point
    // return the length of the data actually read
    uint32_t ReadNotMovePt(uint8_t* data, uint32_t len);
    // move read point
    // return the length of the data actually move
    uint32_t MoveReadPt(int32_t len);
    // return the length of the data actually read
    uint32_t Read(uint8_t* data, uint32_t len);
    // return remaining length of readable data
    uint32_t GetDataLength();
    // return readable buffer list
    std::shared_ptr<BufferBlock> GetReadBuffers();

    // return the length of the actual write
    uint32_t Write(uint64_t offset, uint8_t* data, uint32_t len);
private:
    // return the length of the data actually move
    uint32_t MoveWritePt(int32_t len);

protected:
    SortSegment _sort_segment;
    std::shared_ptr<BufferBlock> _read_pos;
    std::shared_ptr<BufferBlock> _write_pos;
    
    LinkedList<BufferBlock> _buffer_list;
    std::shared_ptr<BlockMemoryPool> _alloter;
};

}

#endif