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
    SortSegment(): _start_offset(0), _limit_offset(0) {}
    ~SortSegment() {}

    // insert a part of segment from offset
    bool Insert(uint64_t offset, uint32_t len);
    // remove a part of segment from start
    uint32_t Remove(uint32_t len);
    // get continuous segment length from start
    uint64_t ContinuousLength();
    // get current start offset on stream 
    uint64_t GetStartOffset() { return _start_offset; }
    // get current limit offset of stream
    uint64_t GetLimitOffset() { return _limit_offset; }
    // update current limit offset of stream, only increase
    bool UpdateLimitOffset(uint64_t offset);

protected:
    enum SegmentType {
        ST_END = 0,
        ST_START = 1,
    };
    uint64_t _start_offset;
    uint64_t _limit_offset;
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

    // return the length of the actual write
    uint32_t Write(uint64_t offset, uint8_t* data, uint32_t len);
private:
    // move 
    // return the length of the data actually move
    uint32_t MoveWritePt(uint64_t offset);

protected:
    uint64_t _cur_write_offset;
    SortSegment _sort_segment;
    std::shared_ptr<BufferBlock> _read_pos;
    std::shared_ptr<BufferBlock> _write_pos;
    
    LinkedList<BufferBlock> _buffer_list;
    std::shared_ptr<BlockMemoryPool> _alloter;
};

}

#endif