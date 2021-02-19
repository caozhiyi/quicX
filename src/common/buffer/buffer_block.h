#ifndef COMMON_BUFFER_BUFFER_BLOCK
#define COMMON_BUFFER_BUFFER_BLOCK

#include <memory>
#include "buffer_interface.h"

namespace quicx {

class BlockMemoryPool;
class BufferBlock : public Buffer {
public:
    BufferBlock(std::shared_ptr<BlockMemoryPool>& alloter);
    ~BufferBlock();

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
    // res1: point to memory fo start.
    // len1: length of memory.
    // there may be two blocks
    bool GetFreeMemoryBlock(void*& res1, uint32_t& len1, void*& res2, uint32_t& len2);

    // get used memory block, 
    // res1: point to memory fo start.
    // len1: length of memory.
    // there may be two blocks
    bool GetUseMemoryBlock(void*& res1, uint32_t& len1, void*& res2, uint32_t& len2);

    // return can read bytes
    uint32_t FindStr(const char* s, uint32_t s_len);

    // list point
    std::shared_ptr<BufferBlock> GetNext();
    void SetNext(std::shared_ptr<BufferBlock> next);
private:
    //find str in fix length buffer. return the first pos if find otherwise return nullptr
    const char* _FindStrInMem(const char* buffer, const char* ch, uint32_t buffer_len, uint32_t ch_len) const;
    uint32_t _Read(char* res, uint32_t len, bool clear);
    uint32_t _Write(const char* str, uint32_t len, bool write);

private:
    uint32_t _total_size;       //total buffer size
    char*    _read;             //read pos
    char*    _write;            //write pos
    char*    _buffer_start;
    char*    _buffer_end;
    bool     _can_read;         //when _read == _write? Is there any data can be read.
    std::shared_ptr<BufferBlock>     _next;         //point to next node
    std::shared_ptr<BlockMemoryPool> _alloter;
};
}

#endif