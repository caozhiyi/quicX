#include <cstring>
#include <cstdlib> // for abort
#include "common/buffer/buffer_read_view.h"
#include "common/buffer/buffer_write_view.h"

namespace quicx {

BufferReadView::BufferReadView(const uint8_t* start, const uint8_t* end):
    _read_pos((uint8_t*)start),
    _buffer_start((uint8_t*)start),
    _buffer_end((uint8_t*)end) {

}

BufferReadView::~BufferReadView() {
    // view do nothing
}

uint32_t BufferReadView::ReadNotMovePt(uint8_t* data, uint32_t len) {
    return Read(data, len, false);
}

uint32_t BufferReadView::MoveReadPt(int32_t len) {
    /*s-----------r-------------------e*/
    if (_read_pos <= _buffer_end) {
        size_t size = _buffer_end - _read_pos;
        if (size <= len) {
            _read_pos += size;
            return (uint32_t)size;

        } else {
            _read_pos += len;
            return len;
        }

    } else {
        // shouldn't be here
        abort();
        return 0;
    }
}

uint32_t BufferReadView::Read(uint8_t* data, uint32_t len) {
    if (data == nullptr) {
        return 0;
    }
    return Read(data, len, true);
}

uint32_t BufferReadView::GetDataLength() {
    /*s-----------r-------------------e*/
    if (_read_pos <= _buffer_end) {
        return uint32_t(_buffer_end - _read_pos);

    } else {
        // shouldn't be here
        abort();
        return 0;
    }
}

std::pair<const uint8_t*, const uint8_t*> BufferReadView::GetReadPair() {
    return std::make_pair(_read_pos, _buffer_end);
}

BufferReadView BufferReadView::GetReadView(uint32_t offset) {
    return std::move(BufferReadView(_buffer_start + offset, _buffer_end));
}

std::shared_ptr<IBufferRead> BufferReadView::GetReadViewPtr(uint32_t offset) {
    return std::make_shared<BufferReadView>(_buffer_start + offset, _buffer_end);
}

const uint8_t* BufferReadView::GetData() {
    return _read_pos;
}

uint32_t BufferReadView::Read(uint8_t* data, uint32_t len, bool move_pt) {
    /*s-----------r-----w-------------e*/
    if (_read_pos <= _buffer_end) {
        size_t size = _buffer_end - _read_pos;
        // data can load all
        if (size <= len) {
            memcpy(data, _read_pos, size);
            if(move_pt) {
                _read_pos += size;
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

}
