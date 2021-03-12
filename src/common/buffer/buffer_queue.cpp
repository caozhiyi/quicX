#include "buffer_queue.h"
#include "buffer_block.h"
#include "alloter/pool_block.h"
#include "alloter/alloter_interface.h"

namespace quicx {

BufferQueue::BufferQueue(const std::shared_ptr<BlockMemoryPool>& block_pool, 
    const std::shared_ptr<AlloterWrap>& alloter): 
    _buffer_count(0),
    _buffer_read(nullptr),
    _buffer_write(nullptr),
    _buffer_end(nullptr),
    _block_alloter(block_pool),
    _alloter(alloter) {

}

BufferQueue::~BufferQueue() {

}

uint32_t BufferQueue::ReadNotMovePt(char* res, uint32_t len) {
    if (!_buffer_read) {
        return 0;
    }

    std::shared_ptr<BufferBlock> temp = _buffer_read;
    uint32_t read_len = 0;
    while (temp && read_len < len) {
        read_len += temp->ReadNotMovePt(res + read_len, len - read_len);
        if (temp == _buffer_write) {
            break;
        }
        temp = temp->GetNext();
    }
    return read_len;
}

uint32_t BufferQueue::Read(std::shared_ptr<Buffer> buffer, uint32_t len) {
    if (len == 0) {
        len = GetCanReadLength();
    }
    if (len == 0) {
        return 0;
    }

    std::shared_ptr<BufferQueue> buffer_queue = std::dynamic_pointer_cast<BufferQueue>(buffer);

    uint32_t can_write_size = buffer_queue->_buffer_write->GetCanWriteLength();
    uint32_t total_read_len = 0;
    uint32_t cur_read_len = 0;

    while (_buffer_read) {
        cur_read_len = _buffer_read->Read(buffer_queue->_buffer_write, can_write_size);
        total_read_len += cur_read_len;

        // current write block is full
        if (cur_read_len >= can_write_size) {
            if (total_read_len >= len) {
                break;
            }

            buffer_queue->Append();
            buffer_queue->_buffer_write = buffer_queue->_buffer_end;
            can_write_size = buffer_queue->_buffer_write->GetCanWriteLength();

        // current read block is empty
        } else {
            can_write_size -= cur_read_len;
            if (_buffer_read == _buffer_write) {
                if (_buffer_write->GetNext()) {
                    _buffer_write = _buffer_write->GetNext();

                } else {
                    Reset();
                    break;
                }
            }
            _buffer_read = _buffer_read->GetNext();
            _buffer_count--;
        }

        if (total_read_len >= len) {
            break;
        }
    }
    return total_read_len;
}

uint32_t BufferQueue::Write(std::shared_ptr<Buffer> buffer, uint32_t len) {
    if (len == 0) {
        len = buffer->GetCanReadLength();
    }
    if (len == 0) {
        return 0;
    }

    std::shared_ptr<BufferQueue> buffer_queue = std::dynamic_pointer_cast<BufferQueue>(buffer);
    std::shared_ptr<BufferBlock> prv_temp;

    uint32_t should_write_size = buffer_queue->_buffer_read->GetCanReadLength();
    uint32_t total_write_len = 0;
    uint32_t cur_write_len = 0;

    while (1) {
        if (!_buffer_write) {
            Append();
            _buffer_write = _buffer_end;

            if (prv_temp) {
                prv_temp->SetNext(_buffer_write);
            }
            prv_temp = _buffer_write;
        }

        // set buffer read to first
        if (!_buffer_read) {
            _buffer_read = _buffer_write;
        }

        cur_write_len = _buffer_write->Write(buffer_queue->_buffer_read, should_write_size);
        total_write_len += cur_write_len;
        
        // current read block is empty
        if (cur_write_len >= should_write_size) {
            if (buffer_queue->_buffer_read == buffer_queue->_buffer_write) {
                if (buffer_queue->_buffer_write->GetNext()) {
                    buffer_queue->_buffer_write = buffer_queue->_buffer_write->GetNext();

                } else {
                    Reset();
                    break;
                }
            }
            buffer_queue->_buffer_read = buffer_queue->_buffer_read->GetNext();
            buffer_queue->_buffer_count--;

            should_write_size = buffer_queue->_buffer_read->GetCanReadLength();

        // current write block is full
        } else {
            if (total_write_len >= len) {
                break;
            }
            should_write_size -= cur_write_len;
            _buffer_write = _buffer_write->GetNext();
        }

        if (total_write_len >= len) {
            break;
        }
    }
    return total_write_len;
}

uint32_t BufferQueue::Read(char* res, uint32_t len) {
    if (!_buffer_read) {
        return 0;
    }

    uint32_t total_read_len = 0;
    while (_buffer_read) {
        total_read_len += _buffer_read->Read(res + total_read_len, len - total_read_len);
        if (total_read_len >= len) {
            break;
        }
        if (_buffer_read == _buffer_write) {
            if (_buffer_write->GetNext()) {
                _buffer_write = _buffer_write->GetNext();

            } else {
                Reset();
                break;
            }
        }
        _buffer_read = _buffer_read->GetNext();
        _buffer_count--;
    }
    return total_read_len;
}

uint32_t BufferQueue::Write(const char* str, uint32_t len) {
    std::shared_ptr<BufferBlock> prv_temp;
    uint32_t write_len = 0;

    while (1) {
        if (!_buffer_write) {
            Append();
            _buffer_write = _buffer_end;
        }
        if (prv_temp) {
            prv_temp->SetNext(_buffer_write);
        }

        write_len += _buffer_write->Write(str + write_len, len - write_len);

        // set buffer read to first
        if (!_buffer_read) {
            _buffer_read = _buffer_write;
        }

        prv_temp = _buffer_write;
        if (write_len >= len) {
            break;
        }
        _buffer_write = _buffer_write->GetNext();
    }
    return write_len;
}


void BufferQueue::Clear() {
    Reset();
}

int32_t BufferQueue::MoveReadPt(int32_t len) {
    uint32_t total_read_len = 0;
    while (_buffer_read) {
        total_read_len += _buffer_read->MoveReadPt(len - total_read_len);

        if (total_read_len >= len) {
            break;
        }

        if (_buffer_read == _buffer_write) {
            if (_buffer_write->GetNext()) {
                _buffer_write = _buffer_write->GetNext();

            } else {
                Reset();
                break;
            }
        }
        _buffer_read = _buffer_read->GetNext();
        _buffer_count--;
    }
    return total_read_len;
}

int32_t BufferQueue::MoveWritePt(int32_t len) {
    uint32_t total_write_len = 0;
    while (_buffer_write) {
        total_write_len += _buffer_write->MoveWritePt(len - total_write_len);
        if (_buffer_write == _buffer_end || len <= total_write_len) {
            break;
        }
        _buffer_write = _buffer_write->GetNext();
    }
    return total_write_len;
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
    
uint32_t BufferQueue::GetCanWriteLength() {
    if (!_buffer_write) {
        return 0;
    }
    
    std::shared_ptr<BufferBlock> temp = _buffer_write;
    uint32_t total_len = 0;
    while (temp) {
        total_len += temp->GetCanWriteLength();
        if (temp == _buffer_end) {
            break;
        }
        temp = temp->GetNext();
    }
    return total_len;
}
    
uint32_t BufferQueue::GetCanReadLength() {
    if (!_buffer_read) {
        return 0;
    }

    std::shared_ptr<BufferBlock> temp = _buffer_read;
    uint32_t total_len = 0;
    while (temp) {
        total_len += temp->GetCanReadLength();
        if (temp == _buffer_write) {
            break;
        }
        temp = temp->GetNext();
    }
    return total_len;
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
                Append();
                temp = _buffer_end;
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

std::shared_ptr<BlockMemoryPool> BufferQueue::GetBlockMemoryPool() {
    return _block_alloter;
}

void BufferQueue::Reset() {
    _buffer_end.reset();
    _buffer_read.reset();
    _buffer_write.reset();
}

void BufferQueue::Append() {
    auto temp = _alloter->PoolNewSharePtr<BufferBlock>(_block_alloter);
    _buffer_count++;
    if (_buffer_end) {
         _buffer_end->SetNext(temp);
    }
    _buffer_end = temp;
}

}