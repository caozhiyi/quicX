// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_BUFFER_BUFFER_SORT_CHAINS
#define COMMON_BUFFER_BUFFER_SORT_CHAINS

#include <map>
#include <vector>
#include "common/buffer/buffer_block.h"
#include "common/buffer/buffer_interface.h"

namespace quicx {

class SortSegment {
public:
    SortSegment(): _cur_offset(0) {}
    ~SortSegment() {}

    bool Insert(uint64_t offset, uint32_t len);
    uint32_t Remove(uint32_t len);
    uint64_t MaxSortLength();
    uint64_t GetCurOffset() { return _cur_offset; }

protected:
    enum SegmentType {
        ST_END = 0,
        ST_START = 1,
    };
    uint64_t _cur_offset;
    std::map<uint64_t, SegmentType> _segment_map;
};

class BufferSortChains {
public:
    BufferSortChains(std::shared_ptr<BlockMemoryPool>& alloter, uint32_t max_size);
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
    virtual uint32_t Write(uint64_t offset, uint8_t* data, uint32_t len);
private:
    // return the length of the actual write
    virtual uint32_t Write(uint8_t* data, uint32_t len);
    // return the remaining length that can be written
    virtual uint32_t GetFreeLength();
    // return the length of the data actually move
    virtual uint32_t MoveWritePt(int32_t len);
    // return buffer write list
    virtual std::shared_ptr<BufferBlock> GetWriteBuffers(uint32_t len);

private:
    int16_t Next(int16_t index);
    int16_t Prev(int16_t index);

private:
    int16_t _read_index;
    int16_t _write_index;
    SortSegment _sort_segment;
    std::shared_ptr<BlockMemoryPool> _alloter;
    std::vector<std::shared_ptr<IBuffer>> _buffers_cycle_list;
};

}

#endif