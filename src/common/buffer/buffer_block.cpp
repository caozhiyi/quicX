#include <cstring>
#include "buffer_block.h"
#include "alloter/pool_block.h"

namespace quicx {

BufferBlock::BufferBlock(std::shared_ptr<BlockMemoryPool>& alloter) : 
    _alloter(alloter), 
    _can_read(false),
    _next(nullptr) {

    _buffer_start = (char*)_alloter->PoolLargeMalloc();
    _total_size = _alloter->GetBlockLength();
    _buffer_end = _buffer_start + _total_size;
    _read = _write = _buffer_start;
}

BufferBlock::~BufferBlock() {
    if (_buffer_start) {
        void* m = (void*)_buffer_start;
        _alloter->PoolLargeFree(m);
    }
}

uint32_t BufferBlock::ReadNotClear(char* res, uint32_t len) {
    return _Read(res, len, false);
}

uint32_t BufferBlock::Read(char* res, uint32_t len) {
    if (res == nullptr) {
        return 0;
    }
    return _Read(res, len, true);
}

uint32_t BufferBlock::Write(const char* str, uint32_t len) {
    if (str == nullptr) {
        return 0;
    }
    return _Write(str, len, true);
}
        
uint32_t BufferBlock::Clear(uint32_t len) {
    if (len == 0) {
        _write = _read = _buffer_start;
        _can_read = false;
        return 0;
    }

    if (!_buffer_start) {
        return 0;
    }

    if (_read < _write) {
        size_t size = _write - _read;
        // res can load all
        if (size <= len) {
            _write = _read = _buffer_start;
            _can_read = false;
            return (int)size;

        // only read len
        } else {
            _read += len;
            return len;
        }

    } else {
        if(!_can_read && _read == _write) {
            return 0;
        }
        size_t size_start = _write - _buffer_start;
        size_t size_end = _buffer_end - _read;
        size_t size =  size_start + size_end;
        // res can load all
        if (size <= len) {
            _read = _write = _buffer_start;
            _can_read = false;
            return (int)size;

        } else {
            if (len <= size_end) {
                _read += len;
                return len;

            } else {
                size_t left = len - size_end;
                _read = _buffer_start + left;
                return len;
            }
        }
    }
}

uint32_t BufferBlock::MoveWritePt(uint32_t len) {
    return _Write(nullptr, len, false);
}

uint32_t BufferBlock::ReadUntil(char* res, uint32_t len) {
    if (GetCanReadLength() < len) {
        return 0;
    
    } else {
        return Read(res, len);
    }
}
    
uint32_t BufferBlock::ReadUntil(char* res, uint32_t len, const char* find, uint32_t find_len, uint32_t& need_len)  {
    uint32_t size = FindStr(find, find_len);
    if (size) {
        if (size <= len) {
            return Read(res, len);
        
        } else {
            need_len = size;
            return 0;
        }
    }

    return 0;
}
    
uint32_t BufferBlock::GetFreeLength() {
    if (_write > _read) {
        return (uint32_t)((_buffer_end - _write) + (_read - _buffer_start));
    
    } else if (_write < _read) {
        return (uint32_t)((_read - _write));

    } else {
        if (_can_read) {
            return 0;
        
        } else {
            return _total_size;
        }
    }
}

uint32_t BufferBlock::GetCanReadLength() {
    if (_write > _read) {
        return (uint32_t)(_write - _read);

    } else if (_write < _read) {
        return (uint32_t)((_buffer_end - _read) + (_write - _buffer_start));

    } else {
        if (_can_read) {
            return _total_size;

        } else {
            return 0;
        }
    }
}

bool BufferBlock::GetFreeMemoryBlock(void*& res1, uint32_t& len1, void*& res2, uint32_t& len2) {
    res1 = res2 = nullptr;
    len1 = len2 = 0;

    if (_write >= _read) {
        if (_can_read && _write == _read) {
            return false;
        }
        res1 = _write;
        len1 = (uint32_t)(_buffer_end - _write);

        len2 = (uint32_t)(_read - _buffer_start);
        if(len2 > 0) {
            res2 = _buffer_start;
        }
        return true;

    } else {
        res1 = _write;
        len1 = (uint32_t)(_read - _write);
        return true;
    }
}

bool BufferBlock::GetUseMemoryBlock(void*& res1, uint32_t& len1, void*& res2, uint32_t& len2) {
    res1 = res2 = nullptr;
    len1 = len2 = 0;

    if (_read >= _write) {
        if (!_can_read && _write == _read) {
            return false;
        }
        res1 = _read;
        len1 = (uint32_t)(_buffer_end - _read);

        len2 = (uint32_t)(_write - _buffer_start);
        if(len2 > 0) {
            res2 = _buffer_start;
        }
        return true;

    } else {
        res1 = _read;
        len1 = (uint32_t)(_write - _read);
        return true;
    }
}

uint32_t BufferBlock::FindStr(const char* s, uint32_t s_len) {
    if (_write > _read) {
        const char* find = _FindStrInMem(_read, s, _write - _read, s_len);
        if (find) {
            return (int)(find - _read + s_len);
        }
        return 0;
        
    } else if (_write < _read) {
        const char* find = _FindStrInMem(_read, s, _buffer_end - _read, s_len);
        if (find) {
            return find - _read + s_len;
        }
        find = _FindStrInMem(_buffer_start, s, _write - _buffer_start, s_len);
        if (find) {
            return find - _buffer_start + s_len + _buffer_end - _read;
        }
        return 0;

    } else {
        if (_can_read) {
            const char* find = _FindStrInMem(_read, s, _buffer_end - _read, s_len);
            if (find) {
                return find - _read + s_len;
            }
            find = _FindStrInMem(_buffer_start, s, _write - _buffer_start, s_len);
            if (find) {
                return find - _buffer_start + s_len + _buffer_end - _read;
            }
            return 0;

        } else {
            return 0;
        }
    }
}

std::shared_ptr<BufferBlock> BufferBlock::GetNext() {
    return _next;
}

void BufferBlock::SetNext(std::shared_ptr<BufferBlock> next) {
    _next = next;
}

const char* BufferBlock::_FindStrInMem(const char* buffer, const char* ch, uint32_t buffer_len, uint32_t ch_len) const {
    if (!buffer) {
        return nullptr;
    }

    const char* buff = buffer;
    const char* find = nullptr;
    size_t finded = 0;
    while(true) {
        find = (char*)memchr(buff, *ch, buffer_len - finded);
        if (!find) {
            break;
        }
        if (memcmp(find, ch, ch_len) == 0) {
            return find;
        }
        finded += find - buff + 1;
        if (buffer_len - finded < ch_len) {
            break;
        }
        buff = ++find;
    }
    return nullptr;
}
    
uint32_t BufferBlock::_Read(char* res, uint32_t len, bool move_pt) {
    if (!_buffer_start) {
        return 0;
    }
    /*s-----------r-----w-------------e*/
    if (_read < _write) {
        size_t size = _write - _read;
        // res can load all
        if (size <= len) {
            memcpy(res, _read, size);
            if(move_pt) {
                // reset point
                _write = _read = _buffer_start;
                _can_read = false;
            }
            return (uint32_t)size;

        // only read len
        } else {
            memcpy(res, _read, len);
            if(move_pt) {
                _read += len;
            }
            return len;
        }

    /*s-----------w-----r-------------e*/
    /*s----------------wr-------------e*/
    } else {
        if(!_can_read && _read == _write) {
            return 0;
        }
        size_t size_start = _write - _buffer_start;
        size_t size_end = _buffer_end - _read;
        size_t size =  size_start + size_end;
        // res can load all
        if (size <= len) {
            memcpy(res, _read, size_end);
            memcpy(res + size_end, _buffer_start, size_start);
            if(move_pt) {
                // reset point
                _read = _write = _buffer_start;
                _can_read = false;
            }
            return (uint32_t)size;

        } else {
            if (len <= size_end) {
                memcpy(res, _read, len);
                if(move_pt) {
                    _read += len;
                }
                return len;

            } else {
                size_t left = len - (size_end);
                memcpy(res, _read, size_end);
                memcpy(res + size_end, _buffer_start, left);
                if(move_pt) {
                    _read = _buffer_start + left;
                }
                return len;
            }
        }
    }
}
    
uint32_t BufferBlock::_Write(const char* str, uint32_t len, bool write) {
    if (!_buffer_start) {
        return 0;
    }
    /*s-----------r-----w-------------e*/
    /*sr----------------w-------------e*/
    if (_read < _write) {
        // w-e can save all data
        if (_write + len <= _buffer_end) {
            if (write) {
                memcpy(_write, str, len);
            }
            _write += len;
            return len;
        
        } else {
            size_t size_end = _buffer_end - _write;
            size_t left = len - size_end;
            if (write) {
                memcpy(_write, str, size_end);
            }
            _write += size_end;

            /*s-----------r-----w-------------e*/
            size_t can_save = _read - _buffer_start;
            // s-r can sava all data
            if (_buffer_start + left <= _read) {
                if (write) {
                    memcpy(_buffer_start, str + size_end, left);
                }
                _write = _buffer_start + left;
                if (_write == _read) {
                    _can_read = true;
                }
                return len;

            // s-r can sava a part of data
            } else {
                if (can_save > 0) {
                    if (write) {
                        memcpy(_buffer_start, str + size_end, can_save);
                    }
                    _write = _read;
                    _can_read = true;
                }
                
                return  (uint32_t)(can_save + size_end);
            }
        }
    
    /*s-----------w-----r-------------e*/
    } else if (_read > _write) {
        size_t size = _read - _write;
        // w-r can save all data
        if (len <= size) {
            if (write) {
                memcpy(_write, str, len);
            }
            _write += len;
            if (_write == _read) {
                _can_read = true;
            }
            return len;

        // w-r can save a part of data
        } else {
            if (write) {
                memcpy(_write, str, size);
            }
            _write += size;
            _can_read = true;

            return (uint32_t)size;
        }
    
    /*s-----------wr-------------------e*/
    } else {
        // there is no free memory
        if (_can_read) {
            return 0;

        } else {
            // reset
            _read = _write = _buffer_start;
            size_t size = _total_size;
            // s-e can save a part of data
            if (size <= len) {
                if (write) {
                    memcpy(_write, str, size);
                }
                _write += size;
                _can_read = true;
                return size;

            // s-e can save all data
            } else {
                if (write) {
                    memcpy(_write, str, len);
                }
                _write += len;
                return len;
            }
        }
    }
}

}