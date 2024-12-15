#include <cstring>
#include <cstdlib> // for abort
#include "common/buffer/buffer_read_view.h"
#include "common/buffer/buffer_write_view.h"

namespace quicx {
namespace common {

BufferWriteView::BufferWriteView(uint8_t* start, uint8_t* end):
    write_pos_(start),
    buffer_start_(start),
    buffer_end_(end) {

}

BufferWriteView::~BufferWriteView() {
    // view do nothing
}

uint32_t BufferWriteView::Write(uint8_t* data, uint32_t len) {
    /*s-----------w-------------------e*/
    if (write_pos_ < buffer_end_) {
        size_t size = buffer_end_ - write_pos_;
        // can save all data
        if (len <= size) {
            memcpy(write_pos_, data, len);
            write_pos_ += len;
            return len;

        // can save a part of data
        } else {
            memcpy(write_pos_, data, size);
            write_pos_ += size;

            return (uint32_t)size;
        }

    /*s------------------------------we*/
    } else if (write_pos_ == buffer_end_) {
        return 0;

    } else {
        // shouldn't be here
        abort();
        return 0;
    }
}

uint32_t BufferWriteView::GetFreeLength() {
    /*s-----------w-------------------e*/
    if (write_pos_ <= buffer_end_) {
        return uint32_t(buffer_end_ - write_pos_);

    } else {
        // shouldn't be here
        abort();
        return 0;
    }
}

uint32_t BufferWriteView::MoveWritePt(int32_t len) {
    if (buffer_end_ < write_pos_) {
        // shouldn't be here
        abort();
    }
    
    size_t size = buffer_end_ - write_pos_;
    if (size >= len) {
        write_pos_ += len;
    } else {
        write_pos_ += size;
    }
    return (uint32_t)(buffer_end_ - write_pos_);
}

BufferSpan BufferWriteView::GetWriteSpan() {
    return std::move(BufferSpan(write_pos_, buffer_end_));
}

BufferWriteView BufferWriteView::GetWriteView(uint32_t offset) {
    return std::move(BufferWriteView(buffer_start_ + offset, buffer_end_));
}

std::shared_ptr<common::IBufferWrite> BufferWriteView::GetWriteViewPtr(uint32_t offset) {
    return std::make_shared<BufferWriteView>(buffer_start_ + offset, buffer_end_);
}

}
}