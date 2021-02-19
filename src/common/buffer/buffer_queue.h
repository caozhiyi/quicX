#ifndef COMMON_BUFFER_BUFFER_QUEUE
#define COMMON_BUFFER_BUFFER_QUEUE

#include <list>
#include <memory>
#include "buffer_interface.h"
#include "network/io_handle.h"
#include "alloter/alloter_interface.h"

namespace quicx {

class AlloterWrap;
class BufferBlock;
class BlockMemoryPool;
class BufferQueue : public Buffer {
public:
    BufferQueue(std::shared_ptr<BlockMemoryPool>& block_pool, 
    std::shared_ptr<AlloterWrap>& alloter);
    ~BufferQueue();

    // read to res buf but don't chenge the read point
    // return read size
    uint32_t ReadNotClear(char* res, uint32_t len);

    uint32_t Read(char* res, uint32_t len);
    uint32_t Write(const char* str, uint32_t len);
        
    // clear all if len
    // or modify read point
    uint32_t Clear(uint32_t len);
    
    // move write point
    uint32_t MoveWritePt(uint32_t len);

    // do not read when buffer less than len. 
    // return len when read otherwise return 0
    uint32_t ReadUntil(char* res, uint32_t len);
    
    // do not read when can't find specified character.
    // return read bytes when read otherwise return 0
    // when find specified character but res'length is too short, 
    // return 0 and the last param return need length
    uint32_t ReadUntil(char* res, uint32_t len, const char* find, uint32_t find_len, uint32_t& need_len);
    
    uint32_t GetFreeLength();
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
    void _Reset();

private:
    uint32_t _buffer_count;
    std::shared_ptr<BufferBlock> _buffer_read;
    std::shared_ptr<BufferBlock> _buffer_write;
    std::shared_ptr<BufferBlock> _buffer_end;
    
    std::shared_ptr<BlockMemoryPool> _block_alloter;
    std::shared_ptr<AlloterWrap> _alloter;
};
}

#endif