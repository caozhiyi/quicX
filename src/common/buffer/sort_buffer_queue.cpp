#include "buffer_block.h"
#include "sort_buffer_queue.h"
#include "common/log/log_interface.h"

namespace quicx {

SortBufferQueue::SortBufferQueue(const std::shared_ptr<BlockMemoryPool>& block_pool, 
    const std::shared_ptr<AlloterWrap>& alloter):
    BufferQueue(block_pool, alloter),
    _data_offset(0),
    _sort_offset(0) {

}
    
SortBufferQueue::~SortBufferQueue() {

}

uint32_t SortBufferQueue::ReadNotMovePt(char* res, uint32_t len) {
    uint32_t sort_length = _sort_offset - _data_offset;
    if (len > sort_length) {
        len = sort_length;
    }
    
    return BufferQueue::ReadNotMovePt(res, len);
}

uint32_t SortBufferQueue::Read(std::shared_ptr<Buffer> buffer, uint32_t len) {
    uint32_t sort_length = _sort_offset - _data_offset;
    if (len == 0 || len > sort_length) {
        len = sort_length;
    }

    uint32_t read_len = BufferQueue::Read(buffer, len);
    _data_offset += read_len;

    return read_len;
}

uint32_t SortBufferQueue::Write(std::shared_ptr<Buffer> buffer, uint64_t data_offset, uint32_t len) {
    if (len == 0) {
        len = buffer->GetCanReadLength();
    }

    if (data_offset < _sort_offset) {
        LOG_INFO("duplicate data");
        return 0;

    // write discontinuous data
    } else if (data_offset > _sort_offset) {
        // check duplicate data
        auto iter = _write_pos_map.find(_sort_offset);
        if (iter != _write_pos_map.end()) {
            LOG_INFO("duplicate data");
            return 0;
        }

        int32_t write_pos = (int32_t)(data_offset - _sort_offset);

        MoveWritePt(write_pos);
        int32_t write_len = (int32_t)BufferQueue::Write(buffer, len);
        MoveWritePt(-(write_pos + write_len));

        _write_pos_map[data_offset] = write_len;

        return (uint32_t)write_len;

    // write continuous data
    } else {
        uint32_t write_len = BufferQueue::Write(buffer, len);
        _sort_offset += write_len;

        // check already wrote data
        while (1) {
            auto iter = _write_pos_map.find(_sort_offset);
            if (iter != _write_pos_map.end()) {
                _sort_offset += iter->second;
                _write_pos_map.erase(iter);

            } else {
                break;
            }
        }
        return write_len;
    }
}

uint32_t SortBufferQueue::Read(char* res, uint32_t len) {
    uint32_t sort_length = _sort_offset - _data_offset;
    if (len > sort_length) {
        len = sort_length;
    }

    uint32_t read_len = BufferQueue::Read(res, len);
    _data_offset += read_len;
    
    return read_len;
}
    
uint32_t SortBufferQueue::Write(const char* data, uint64_t write_pos, uint32_t len) {
    MoveWritePt(write_pos);
    return BufferQueue::Write(data, len);
}

int32_t SortBufferQueue::MoveReadPt(int32_t len) {
    uint32_t sort_length = _sort_offset - _data_offset;
    if (len > sort_length) {
        len = sort_length;
    }

    uint32_t read_len = 0;
    if (len > 0) {
        read_len = BufferQueue::MoveReadPt(len);
        _data_offset += read_len;

    } else {
        read_len = BufferQueue::MoveReadPt(len);
        _data_offset -= read_len;
    }
    
    return read_len;
}

int32_t SortBufferQueue::MoveWritePt(int32_t len) {
    uint32_t total_write_len = 0;
    if (len >= 0) {
        while (_buffer_write) {
            total_write_len += _buffer_write->MoveWritePt(len - total_write_len);
            if (len <= total_write_len) {
                break;
            }
            if (_buffer_write == _buffer_list.GetTail()) {
                Append();
                _buffer_write = _buffer_list.GetTail();

            } else {
                _buffer_write = _buffer_write->GetNext();
            }
        }

    } else {
        while (_buffer_write) {
            total_write_len += _buffer_write->MoveWritePt(len + total_write_len);
            if (_buffer_write == _buffer_list.GetHead() || -len <= total_write_len) {
                break;
            }
            _buffer_write = _buffer_write->GetPrev();
        }
    }

    return total_write_len;
}

uint32_t SortBufferQueue::ReadUntil(char* res, uint32_t len) {
    uint32_t sort_length = _sort_offset - _data_offset;
    if (len > sort_length) {
        len = sort_length;
    }

    uint32_t read_len = BufferQueue::ReadUntil(res, len);
    _data_offset += read_len;

    return read_len;
}

uint32_t SortBufferQueue::GetCanReadLength() {
    return (uint32_t)(_sort_offset - _data_offset);
}

}