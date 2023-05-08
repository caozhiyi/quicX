#ifndef QUIC_STREAM_CACHE_SEGMENT
#define QUIC_STREAM_CACHE_SEGMENT

#include <map>

namespace quicx {

class BufferSegment {
public:
    BufferSegment(): _start_offset(0), _limit_offset(0) {}
    ~BufferSegment() {}

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

}

#endif