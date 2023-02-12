#include <cstring>
#include <cstdlib> // for abort
#include "common/buffer/buffer_read_view.h"
#include "common/buffer/buffer_write_view.h"

namespace quicx {

BufferWriteView::BufferWriteView(uint8_t* start, uint8_t* end):
    _write_pos(start),
    _buffer_start(start),
    _buffer_end(end) {

}

BufferWriteView::~BufferWriteView() {
    // view do nothing
}

uint32_t BufferWriteView::Write(uint8_t* data, uint32_t len) {
    /*s-----------w-------------------e*/
    if (_write_pos < _buffer_end) {
        size_t size = _buffer_end - _write_pos;
        // can save all data
        if (len <= size) {
            memcpy(_write_pos, data, len);
            _write_pos += len;
            return len;

        // can save a part of data
        } else {
            memcpy(_write_pos, data, size);
            _write_pos += size;

            return (uint32_t)size;
        }

    /*s------------------------------we*/
    } else if (_write_pos == _buffer_end) {
        return 0;

    } else {
        // shouldn't be here
        abort();
        return 0;
    }
}

uint32_t BufferWriteView::GetFreeLength() {
    /*s-----------w-------------------e*/
    if (_write_pos <= _buffer_end) {
        return uint32_t(_buffer_end - _write_pos);

    } else {
        // shouldn't be here
        abort();
        return 0;
    }
}

uint32_t BufferWriteView::MoveWritePt(int32_t len) {
    if (_buffer_end < _write_pos) {
        // shouldn't be here
        abort();
    }
    
    size_t size = _buffer_end - _write_pos;
    if (size >= len) {
        _write_pos += len;
    } else {
        _write_pos += size;
    }
    return (uint32_t)(_buffer_end - _write_pos);
}

BufferSpan BufferWriteView::GetWriteSpan() {
    return std::move(BufferSpan(_write_pos, _buffer_end));
}

BufferWriteView BufferWriteView::GetWriteView(uint32_t offset) {
    return std::move(BufferWriteView(_buffer_start + offset, _buffer_end));
}

std::shared_ptr<IBufferWrite> BufferWriteView::GetWriteViewPtr(uint32_t offset) {
    return std::make_shared<BufferWriteView>(_buffer_start + offset, _buffer_end);
}

}