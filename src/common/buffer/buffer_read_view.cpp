#include <cstring>
#include <cstdlib> // for abort
#include "common/buffer/buffer_read_view.h"
#include "common/buffer/buffer_write_view.h"

namespace quicx {
namespace common {

BufferReadView::BufferReadView(BufferSpan span):
    read_pos_(span.GetStart()),
    buffer_start_(span.GetStart()),
    buffer_end_(span.GetEnd()) {

}

BufferReadView::BufferReadView(uint8_t* start, uint32_t len):
    read_pos_(start),
    buffer_start_(start),
    buffer_end_(start + len) {
}

BufferReadView::BufferReadView(uint8_t* start, uint8_t* end):
    read_pos_(start),
    buffer_start_(start),
    buffer_end_(end) {

}

BufferReadView::~BufferReadView() {
    // view do nothing
}

uint32_t BufferReadView::ReadNotMovePt(uint8_t* data, uint32_t len) {
    return Read(data, len, false);
}

uint32_t BufferReadView::MoveReadPt(int32_t len) {
    /*s-----------r-------------------e*/
    if (read_pos_ <= buffer_end_) {
        size_t size = buffer_end_ - read_pos_;
        if (size <= len) {
            read_pos_ += size;
            return (uint32_t)size;

        } else {
            read_pos_ += len;
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
    if (read_pos_ <= buffer_end_) {
        return uint32_t(buffer_end_ - read_pos_);

    } else {
        // shouldn't be here
        abort();
        return 0;
    }
}

BufferSpan BufferReadView::GetReadSpan() {
    return std::move(BufferSpan(read_pos_, buffer_end_));
}

BufferReadView BufferReadView::GetReadView(uint32_t offset) {
    return std::move(BufferReadView(buffer_start_ + offset, buffer_end_));
}

std::shared_ptr<common::IBufferRead> BufferReadView::GetReadViewPtr(uint32_t offset) {
    return std::make_shared<BufferReadView>(buffer_start_ + offset, buffer_end_);
}

uint8_t* BufferReadView::GetData() {
    return read_pos_;
}

uint32_t BufferReadView::Read(uint8_t* data, uint32_t len, bool move_pt) {
    /*s-----------r-----w-------------e*/
    if (read_pos_ <= buffer_end_) {
        size_t size = buffer_end_ - read_pos_;
        // data can load all
        if (size <= len) {
            memcpy(data, read_pos_, size);
            if(move_pt) {
                read_pos_ += size;
            }
            return (uint32_t)size;

        // only read len
        } else {
            memcpy(data, read_pos_, len);
            if(move_pt) {
                read_pos_ += len;
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
}
