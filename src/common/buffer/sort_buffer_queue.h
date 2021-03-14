/*#ifndef COMMON_BUFFER_SORT_BUFFER_QUEUE
#define COMMON_BUFFER_SORT_BUFFER_QUEUE

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
    uint32_t Write(std::shared_ptr<Buffer> buffer, uint32_t write_pos, uint32_t len = 0);

    uint32_t Read(char* res, uint32_t len);
    uint32_t Write(const char* data, uint32_t write_pos, uint32_t len);

    // move read point
    int32_t MoveReadPt(int32_t len);
    // move write point
    int32_t MoveWritePt(int32_t len);

    // do not read when buffer less than len. 
    // return len when read otherwise return 0
    uint32_t ReadUntil(char* res, uint32_t len);
    
    // do not read when can't find specified character.
    // return read bytes when read otherwise return 0
    // when find specified character but res'length is too short, 
    // return 0 and the last param return need length
    uint32_t ReadUntil(char* res, uint32_t len, const char* find, uint32_t find_len, uint32_t& need_len);
    
    uint32_t GetCanWriteLength();
    uint32_t GetCanReadLength();

    // get free memory block, 
    // block_vec: memory block vector.
    // size: count block_vec's memory, bigger than size.
    // if size = 0, return existing free memory block. 
    // return size of free memory. 
    uint32_t GetFreeMemoryBlock(std::vector<Iovec>& block_vec, uint32_t size = 0);

    // get use memory block, 
    // block_vec: memory block vector.
    // return size of use memory. 
    uint32_t GetUseMemoryBlock(std::vector<Iovec>& block_vec, uint32_t max_size = 4096);

    // return can read bytes
    uint32_t FindStr(const char* s, uint32_t s_len);

private:
    uint32_t _sort_length;
};

}

#endif
*/