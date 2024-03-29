#include <cstring>
#include <cstdlib> // for abort
#include "common/buffer/buffer.h"
#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_read_view.h"
#include "common/buffer/buffer_write_view.h"

namespace quicx {
namespace common {

Buffer::Buffer(BufferSpan& span):
    _can_read(false),
    IBuffer(nullptr) {
    _buffer_start = span.GetStart();
    _buffer_end = span.GetEnd();
    _read_pos = _write_pos = _buffer_start;
}

Buffer::Buffer(uint8_t* start, uint32_t len):
    _can_read(false),
    IBuffer(nullptr) {
    _buffer_start = start;
    _buffer_end = start + len;
    _read_pos = _write_pos = _buffer_start;
}

Buffer::Buffer(uint8_t* start, uint8_t* end):
    _can_read(false),
    IBuffer(nullptr) {
    _buffer_start = start;
    _buffer_end = end;
    _read_pos = _write_pos = _buffer_start;
}

Buffer::Buffer(std::shared_ptr<common::BlockMemoryPool>& alloter):
    _can_read(false),
    IBuffer(alloter) {

    _buffer_start = (uint8_t*)alloter->PoolLargeMalloc();
    _buffer_end = _buffer_start + alloter->GetBlockLength();
    _read_pos = _write_pos = _buffer_start;
}

Buffer::~Buffer() {
    if (_buffer_start != nullptr) {
        auto alloter = _alloter.lock();
        if (alloter) {
            void* m = (void*)_buffer_start;
            alloter->PoolLargeFree(m);
        }
    }
}

uint32_t Buffer::ReadNotMovePt(uint8_t* data, uint32_t len) {
    if (data == nullptr) {
        return 0;
    }
    return InnerRead(data, len, false);
}

uint32_t Buffer::MoveReadPt(int32_t len) {
    if (!_buffer_start) {
        return 0;
    }

    if (len > 0) {
        if (_read_pos <= _write_pos) {
            size_t size = _write_pos - _read_pos;
            // all buffer will be used
            if ((int32_t)size <= len) {
                Clear();
                return (int32_t)size;

            // part of buffer will be used
            } else {
                _read_pos += len;
                return len;
            }

        } else {
            // shouldn't be here
            abort();
            return 0;
        }

    } else {
        len = -len;
        if (_buffer_start <= _read_pos) {
            size_t size = _read_pos - _buffer_start;
            // reread all buffer
            if ((int32_t)size <= len) {
                _read_pos -= size;
                return (int32_t)size;

            // only reread part of buffer
            } else {
                _read_pos -= len;
                return len;
            }

        } else {
            // shouldn't be here
            abort();
            return 0;
        }
    }
}

uint32_t Buffer::Read(uint8_t* data, uint32_t len) {
    if (data == nullptr) {
        return 0;
    }
    return InnerRead(data, len, true);
}

uint32_t Buffer::GetDataLength() {
    return uint32_t(_write_pos - _read_pos); 
}

BufferSpan Buffer::GetReadSpan() {
    return std::move(BufferSpan(_read_pos, _write_pos));
}

BufferReadView Buffer::GetReadView(uint32_t offset) {
    return std::move(BufferReadView(_read_pos, _write_pos));
}

std::shared_ptr<common::IBufferRead> Buffer::GetReadViewPtr(uint32_t offset) {
    return std::make_shared<BufferReadView>(_read_pos, _write_pos);
}

uint8_t* Buffer::GetData() {
    return _read_pos;
}

// clear all data
void Buffer::Clear() {
    _write_pos = _read_pos = _buffer_start;
    _can_read = false;
}

uint32_t Buffer::Write(uint8_t* data, uint32_t len) {
    if (data == nullptr) {
        return 0;
    }
    return InnerWrite((uint8_t*)data, len);
}

uint32_t Buffer::GetFreeLength() {
    return uint32_t(_buffer_end - _write_pos);
}

uint32_t Buffer::MoveWritePt(int32_t len) {
    if (!_buffer_start) {
        return 0;
    }

    if (len > 0) {
        if (_write_pos <= _buffer_end) {
            size_t size = _buffer_end - _write_pos;
            // all buffer will be used
            if ((int32_t)size <= len) {
                _write_pos += size;
                _can_read = true;
                return (int32_t)size;

            // part of buffer will be used
            } else {
                _write_pos += len;
                return len;
            }

        } else {
            // shouldn't be here
            abort();
            return 0;
        }

    } else {
        len = -len;
        if (_read_pos <= _write_pos) {
            size_t size = _write_pos - _read_pos;
            // rewrite all buffer
            if ((int32_t)size <= len) {
                Clear();
                return (int32_t)size;

            // only rewrite part of buffer
            } else {
                _write_pos -= len;
                return len;
            }
        
        } else {
            // shouldn't be here
            abort();
            return 0;
        }
    }
}

BufferSpan Buffer::GetWriteSpan() {
    return std::move(BufferSpan(_write_pos, _buffer_end));
}

BufferWriteView Buffer::GetWriteView(uint32_t offset) {
    return std::move(BufferWriteView(_write_pos, _buffer_end));
}

std::shared_ptr<common::IBufferWrite> Buffer::GetWriteViewPtr(uint32_t offset) {
    return std::make_shared<BufferWriteView>(_write_pos, _buffer_end);
}

uint32_t Buffer::InnerRead(uint8_t* data, uint32_t len, bool move_pt) {
    /*s-----------r-----w-------------e*/
    if (_read_pos <= _write_pos) {
        size_t size = _write_pos - _read_pos;
        // res can load all
        if (size <= len) {
            memcpy(data, _read_pos, size);
            if(move_pt) {
                Clear();
            }
            return (uint32_t)size;

        // only read len
        } else {
            memcpy(data, _read_pos, len);
            if(move_pt) {
                _read_pos += len;
            }
            return len;
        }

    } else {
        // shouldn't be here
        abort();
        return 0;
    }
}

uint32_t Buffer::InnerWrite(uint8_t* data, uint32_t len) {
    if (_write_pos <= _buffer_end) {
        size_t size = _buffer_end - _write_pos;
        // all buffer will be used
        if (size <= len) {
            memcpy(_write_pos, data, size);

            _write_pos += size;
            _can_read = true;
            return (int32_t)size;

        // part of buffer will be used
        } else {
            memcpy(_write_pos, data, len);
            _write_pos += len;
            return len;
        }
    } else {
        // shouldn't be here
        abort();
        return 0;
    }
}

}
}