/*#include "sort_buffer_queue.h"

namespace quicx {

SortBufferQueue::SortBufferQueue(const std::shared_ptr<BlockMemoryPool>& block_pool, 
    const std::shared_ptr<AlloterWrap>& alloter):
    BufferQueue(block_pool, alloter),
    _sort_length(0) {

}
    
SortBufferQueue::~SortBufferQueue() {

}

uint32_t SortBufferQueue::ReadNotMovePt(char* res, uint32_t len) {
    if (len > _sort_length) {
        len = _sort_length;
    }
    
    return BufferQueue::ReadNotMovePt(res, len);
}

uint32_t SortBufferQueue::Read(std::shared_ptr<Buffer> buffer, uint32_t len) {
    if (len == 0 || len > _sort_length) {
        len = _sort_length;
    }

    uint32_t read_len = BufferQueue::Read(buffer, len);
    _sort_length -= read_len;
    
    return read_len;
}

uint32_t SortBufferQueue::Write(std::shared_ptr<Buffer> buffer, uint32_t write_pos, uint32_t len) {
    if (len == 0) {
        len = buffer->GetCanReadLength();
    }


    
}

uint32_t SortBufferQueue::Read(char* res, uint32_t len) {
    if (len > _sort_length) {
        len = _sort_length;
    }

    uint32_t read_len = BufferQueue::Read(res, len);
    _sort_length -= read_len;
    
    return read_len;
}
    
uint32_t SortBufferQueue::Write(const char* data, uint32_t write_pos, uint32_t len) {

}

int32_t SortBufferQueue::MoveReadPt(int32_t len) {
    if (len > _sort_length) {
        len = _sort_length;
    }

    uint32_t read_len = BufferQueue::MoveReadPt(len);
    _sort_length -= read_len;
    
    return read_len;
}

int32_t SortBufferQueue::MoveWritePt(int32_t len) {
    uint32_t move_size = 0;
    uint32_t left_size = len;

    do {
        move_size = BufferQueue::MoveWritePt(left_size);
        if (move_size < left_size) {
            BufferQueue::Append();
            //_buffer_write = _buffer_end;
        }
        left_size -= move_size;
    } while (left_size > 0);
}

uint32_t SortBufferQueue::ReadUntil(char* res, uint32_t len) {

}
    
uint32_t SortBufferQueue::ReadUntil(char* res, uint32_t len, const char* find, uint32_t find_len, uint32_t& need_len) {

}
    
uint32_t SortBufferQueue::GetCanWriteLength() {

}

uint32_t SortBufferQueue::GetCanReadLength() {

}

uint32_t GetFreeMemoryBlock(std::vector<Iovec>& block_vec, uint32_t size = 0) {

}

uint32_t GetUseMemoryBlock(std::vector<Iovec>& block_vec, uint32_t max_size = 4096) {

}

uint32_t FindStr(const char* s, uint32_t s_len) {

}

std::shared_ptr<BlockMemoryPool> GetBlockMemoryPool() {

}

}
*/