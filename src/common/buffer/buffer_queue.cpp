#include "buffer_queue.h"
#include "buffer_block.h"
#include "alloter/pool_block.h"
#include "alloter/alloter_interface.h"

namespace quicx {

BufferQueue::BufferQueue(std::shared_ptr<BlockMemoryPool>& block_pool, 
    std::shared_ptr<AlloterWrap>& alloter): 
    _buffer_read(nullptr),
    _buffer_write(nullptr),
    _buffer_end(nullptr),
    _block_alloter(block_pool),
    _alloter(alloter) {

}

BufferQueue::~BufferQueue() {

}

uint32_t BufferQueue::ReadNotClear(char* res, uint32_t len) {
    if (!_buffer_read) {
        return 0;
    }

    std::shared_ptr<BufferBlock> temp = _buffer_read;
    uint32_t read_len = 0;
    while (temp && read_len < len) {
        read_len += temp->ReadNotClear(res, len - read_len);
        if (temp == _buffer_write) {
            break;
        }
        temp = temp->GetNext();
    }
    return read_len;
}

uint32_t BufferQueue::Read(char* res, uint32_t len) {
    if (!_buffer_read) {
        return 0;
    }

    std::shared_ptr<BufferBlock> temp = _buffer_read;
    uint32_t read_len = 0;
    while (temp) {
        read_len += temp->Read(res, len - read_len);
        if (read_len >= len) {
            break;
        }
        if (temp == _buffer_write) {
            if (_buffer_write->GetNext()) {
                _buffer_write = _buffer_write->GetNext();

            } else {
                _Reset();
            }
        }
        temp = temp->GetNext();
        _buffer_count--;
    }
    _buffer_read = temp;
    return read_len;
}

uint32_t BufferQueue::Write(const char* str, uint32_t len) {
    std::shared_ptr<BufferBlock> prv_temp;
    std::shared_ptr<BufferBlock> temp = _buffer_write;
    uint32_t write_len = 0;
    while (1) {
        if (temp == nullptr) {
            temp = _alloter->PoolNewSharePtr<BufferBlock>(_block_alloter);
            _buffer_count++;
            // set buffer end to next node
            _buffer_end = temp;
        }
        if (prv_temp) {
            prv_temp->SetNext(temp);
        }

        write_len += temp->Write(str + write_len, len - write_len);

        // set buffer read to first
        if (_buffer_read == nullptr) {
            _buffer_read = temp;
        }

        prv_temp = temp;
        if (write_len >= len) {
            break;
        }
        temp = temp->GetNext();
    }
    _buffer_write = temp;
    return write_len;
}
        
uint32_t BufferQueue::Clear(uint32_t len) {
    if (len == 0) {
        std::shared_ptr<BufferBlock> temp = _buffer_read;
        std::shared_ptr<BufferBlock> cur;
        while (temp) {
            cur = temp;
            temp = temp->GetNext();
            _buffer_count--;
        }
        _Reset();
        return 0;
    }
    
    std::shared_ptr<BufferBlock> temp = _buffer_read;
    std::shared_ptr<BufferBlock> del_temp;
    uint32_t cur_len = 0;
    while (temp) {
        cur_len += temp->Clear(len - cur_len);
        if (cur_len >= len) {
            break;
        }
        if (temp == _buffer_write) {
            if (_buffer_write->GetNext()) {
                _buffer_write = _buffer_write->GetNext();

            } else {
                _Reset();
            }
        }
        del_temp = temp;
        temp = temp->GetNext();
        _buffer_count--;
    }
    _buffer_read = temp;
    return cur_len;
}

uint32_t BufferQueue::MoveWritePt(uint32_t len) {
    std::shared_ptr<BufferBlock> temp = _buffer_write;
    uint32_t cur_len = 0;
    while (temp) {
        cur_len += temp->MoveWritePt(len - cur_len);
        if (temp == _buffer_end || len <= cur_len) {
            break;
        }
        temp = temp->GetNext();
    }
    _buffer_write = temp;
    return cur_len;
}

uint32_t BufferQueue::ReadUntil(char* res, uint32_t len) {
    if (GetCanReadLength() < len) {
        return 0;

    } else {
        return Read(res, len);
    }
}
    
uint32_t BufferQueue::ReadUntil(char* res, uint32_t len, const char* find, uint32_t find_len, uint32_t& need_len) {
    uint32_t size = FindStr(find, find_len);
    if (size) {
        if (size <= len) {
            return Read(res, size);

        } else {
            need_len = size;
            return 0;
        }
    }
    return 0;
}
    
uint32_t BufferQueue::GetFreeLength() {
    if (!_buffer_write) {
        return 0;
    }
    
    std::shared_ptr<BufferBlock> temp = _buffer_write;
    uint32_t cur_len = 0;
    while (temp) {
        cur_len += temp->GetFreeLength();
        if (temp == _buffer_end) {
            break;
        }
        temp = temp->GetNext();
    }
    return cur_len;
}
    
uint32_t BufferQueue::GetCanReadLength() {
    if (!_buffer_read) {
        return 0;
    }

    std::shared_ptr<BufferBlock> temp = _buffer_read;
    uint32_t cur_len = 0;
    while (temp) {
        cur_len += temp->GetCanReadLength();
        if (temp == _buffer_write) {
            break;
        }
        temp = temp->GetNext();
    }
    return cur_len;
}

uint32_t BufferQueue::GetFreeMemoryBlock(std::vector<Iovec>& block_vec, uint32_t size) {
    void* mem_1 = nullptr;
    void* mem_2 = nullptr;
    uint32_t mem_len_1 = 0;
    uint32_t mem_len_2 = 0;

    std::shared_ptr<BufferBlock> temp = _buffer_write;
    std::shared_ptr<BufferBlock> prv_temp;
    uint32_t cur_len = 0;
    if (size > 0) {
        while (cur_len < size) {
            if (temp == nullptr) {
                temp = _alloter->PoolNewSharePtr<BufferBlock>(_block_alloter);
                _buffer_count++;
            }
            if (prv_temp != nullptr) {
                prv_temp->SetNext(temp);
            }
        
            temp->GetFreeMemoryBlock(mem_1, mem_len_1, mem_2, mem_len_2);
            if (mem_len_1 > 0) {
                block_vec.push_back(Iovec(mem_1, mem_len_1));
                cur_len += mem_len_1;
            }
            if (mem_len_2 > 0) {
                block_vec.push_back(Iovec(mem_2, mem_len_2));
                cur_len += mem_len_2;
            }
            // set buffer read to first
            if (_buffer_read == nullptr) {
                _buffer_read = temp;
            }
            // set buffer write to first
            if (_buffer_write == nullptr) {
                _buffer_write = temp;
            }
            prv_temp = temp;
            temp = temp->GetNext();
        }
        _buffer_end = prv_temp;

    } else {
        while (temp) {
            temp->GetFreeMemoryBlock(mem_1, mem_len_1, mem_2, mem_len_2);
            if (mem_len_1 > 0) {
                block_vec.push_back(Iovec(mem_1, mem_len_1));
                cur_len += mem_len_1;
            }
            if (mem_len_2 > 0) {
                block_vec.push_back(Iovec(mem_2, mem_len_2));
                cur_len += mem_len_2;
            }
            if (temp == _buffer_end) {
                break;
            }
            temp = temp->GetNext();
        }
    }
    return cur_len;
}

uint32_t BufferQueue::GetUseMemoryBlock(std::vector<Iovec>& block_vec, uint32_t max_size) {
    void* mem_1 = nullptr;
    void* mem_2 = nullptr;
    uint32_t mem_len_1 = 0;
    uint32_t mem_len_2 = 0;

    std::shared_ptr<BufferBlock> temp = _buffer_read;
    uint32_t cur_len = 0;
    while (temp) {
        temp->GetUseMemoryBlock(mem_1, mem_len_1, mem_2, mem_len_2);
        if (mem_len_1 > 0) {
            block_vec.push_back(Iovec(mem_1, mem_len_1));
            cur_len += mem_len_1;
        }
        if (mem_len_2 > 0) {
            block_vec.push_back(Iovec(mem_2, mem_len_2));
            cur_len += mem_len_2;
        }
        if (temp == _buffer_write) {
            break;
        }
        if (cur_len >= max_size) {
            break;
        }
        temp = temp->GetNext();
    }
    return cur_len;
}

uint32_t BufferQueue::FindStr(const char* s, uint32_t s_len) {
    if (!_buffer_read) {
        return 0;
    }

    std::shared_ptr<BufferBlock> temp = _buffer_read;
    uint32_t cur_len = 0;
    uint32_t can_read = 0;
    while (temp) {
        can_read = temp->FindStr(s, s_len);
        if (can_read > 0) {
            cur_len += can_read;
            break;
        }
        if (temp == _buffer_write) {
            break;
        }
        cur_len += temp->GetCanReadLength();
        temp = temp->GetNext();
    }
    return cur_len;
}

void BufferQueue::_Reset() {
    _buffer_end.reset();
    _buffer_read.reset();
    _buffer_write.reset();
}

}