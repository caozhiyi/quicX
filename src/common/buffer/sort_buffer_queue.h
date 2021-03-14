#ifndef COMMON_BUFFER_SORT_BUFFER_QUEUE
#define COMMON_BUFFER_SORT_BUFFER_QUEUE

#include <unordered_map>
#include "buffer_queue.h"

namespace quicx {

class SortBufferQueue: public BufferQueue {
public:
    SortBufferQueue(const std::shared_ptr<BlockMemoryPool>& block_pool, 
    const std::shared_ptr<AlloterWrap>& alloter);
    ~SortBufferQueue();

    // read to res buf but don't chenge the read point
    // return read size
    uint32_t ReadNotMovePt(char* res, uint32_t len);

    uint32_t Read(std::shared_ptr<Buffer> buffer, uint32_t len = 0);
    // don't call this function
    uint32_t Write(std::shared_ptr<Buffer> buffer, uint32_t len = 0) { return 0; }
    // only use this function
    uint32_t Write(std::shared_ptr<Buffer> buffer, uint64_t data_offset, uint32_t len = 0);

    uint32_t Read(char* res, uint32_t len);
    // don't call this function
    uint32_t Write(const char* data, uint32_t len) { return 0; }
    // only use this function
    uint32_t Write(const char* data, uint64_t data_offset, uint32_t len);

    // move read point
    int32_t MoveReadPt(int32_t len);
    // move write point
    int32_t MoveWritePt(int32_t len);

    // do not read when buffer less than len. 
    // return len when read otherwise return 0
    uint32_t ReadUntil(char* res, uint32_t len);
    
    // don't call this function
    uint32_t GetCanWriteLength() { return 0; }
    uint32_t GetCanReadLength();

    // don't call this function
    uint32_t GetFreeMemoryBlock(std::vector<Iovec>& block_vec, uint32_t size = 0) { return 0; }

    // don't call this function
    uint32_t GetUseMemoryBlock(std::vector<Iovec>& block_vec, uint32_t max_size = 4096) { return 0; }

private:
    uint64_t _data_offset;
    uint64_t _sort_offset;
    std::unordered_map<uint64_t, uint32_t> _write_pos_map;
};

}

#endif