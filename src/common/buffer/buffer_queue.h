#ifndef COMMON_BUFFER_BUFFER_QUEUE
#define COMMON_BUFFER_BUFFER_QUEUE

#include <vector>
#include <memory>
#include "buffer_interface.h"
#include "network/io_handle.h"
#include "alloter/alloter_interface.h"

namespace quicx {

class AlloterWrap;
class BufferBlock;
class BlockMemoryPool;
class BufferQueue: public Buffer {
public:
    BufferQueue(const std::shared_ptr<BlockMemoryPool>& block_pool, 
    const std::shared_ptr<AlloterWrap>& alloter);
    ~BufferQueue();

    // read to res buf but don't chenge the read point
    // return read size
    uint32_t ReadNotMovePt(char* res, uint32_t len);

    uint32_t Read(std::shared_ptr<Buffer> buffer, uint32_t len = 0);
    uint32_t Write(std::shared_ptr<Buffer> buffer, uint32_t len = 0);

    uint32_t Read(char* res, uint32_t len);
    uint32_t Write(const char* data, uint32_t len);
    
    // clear all data
    void Clear();

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

    // return block memory pool
    std::shared_ptr<BlockMemoryPool> GetBlockMemoryPool();

private:
    void Reset();
    void Append();

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