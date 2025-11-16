#include <cstring>

#include "common/log/log.h"
#include "common/buffer/buffer_write_view.h"

namespace quicx {
namespace common {

BufferWriteView::BufferWriteView() = default;

BufferWriteView::BufferWriteView(uint8_t* start, uint32_t len)
    : BufferWriteView(start, start ? start + len : nullptr) {}

BufferWriteView::BufferWriteView(uint8_t* start, uint8_t* end)
    : BufferWriteView() {
    Reset(start, end);
}

void BufferWriteView::Reset(uint8_t* start, uint32_t len) {
    Reset(start, start ? start + len : nullptr);
}

void BufferWriteView::Reset(uint8_t* start, uint8_t* end) {
    buffer_start_ = start;
    buffer_end_ = end;
    write_pos_ = start;

    if (!Valid()) {
        LOG_ERROR("span is invalid");
        buffer_start_ = nullptr;
        buffer_end_ = nullptr;
        write_pos_ = nullptr;
    }
}

uint32_t BufferWriteView::Write(const uint8_t* data, uint32_t len) {
    if (data == nullptr) {
        LOG_ERROR("data buffer is nullptr");
        return 0;
    }
    if (!Valid()) {
        LOG_ERROR("span is invalid");
        return 0;
    }

    const size_t remaining = buffer_end_ - write_pos_;
    const uint32_t copy_len = remaining < len ? static_cast<uint32_t>(remaining) : len;
    if (copy_len == 0) {
        return 0;
    }

    std::memcpy(write_pos_, data, copy_len);
    write_pos_ += copy_len;
    return copy_len;
}

uint32_t BufferWriteView::MoveWritePt(int32_t len) {
    if (!Valid()) {
        LOG_ERROR("span is invalid");
        return 0;
    }

    if (len > 0) {
        const size_t remaining = buffer_end_ - write_pos_;
        if (static_cast<int32_t>(remaining) <= len) {
            write_pos_ = buffer_end_;
            return static_cast<uint32_t>(remaining);
        }
        write_pos_ += len;
        return static_cast<uint32_t>(len);
    }

    len = -len;
    const size_t produced = write_pos_ - buffer_start_;
    if (static_cast<int32_t>(produced) <= len) {
        write_pos_ = buffer_start_;
        return static_cast<uint32_t>(produced);
    }
    write_pos_ -= len;
    return static_cast<uint32_t>(len);
}

uint32_t BufferWriteView::GetFreeLength() const {
    if (!Valid()) {
        LOG_ERROR("span is invalid");
        return 0;
    }
    return static_cast<uint32_t>(buffer_end_ - write_pos_);
}

uint32_t BufferWriteView::GetDataLength() const {
    if (!Valid()) {
        LOG_ERROR("span is invalid");
        return 0;
    }
    return static_cast<uint32_t>(write_pos_ - buffer_start_);
}

uint8_t* BufferWriteView::GetData() const {
    if (!Valid()) {
        LOG_ERROR("span is invalid");
        return nullptr;
    }
    return write_pos_;
}

BufferSpan BufferWriteView::GetWritableSpan() const {
    if (!Valid()) {
        LOG_ERROR("span is invalid");
        return BufferSpan();
    }
    return BufferSpan(write_pos_, buffer_end_);
}

BufferSpan BufferWriteView::GetWrittenSpan() const {
    if (!Valid()) {
        LOG_ERROR("span is invalid");
        return BufferSpan();
    }
    return BufferSpan(buffer_start_, write_pos_);
}

bool BufferWriteView::Valid() const {
    if (buffer_start_ == nullptr || buffer_end_ == nullptr) {
        return false;
    }
    if (buffer_start_ > buffer_end_) {
        return false;
    }
    return write_pos_ != nullptr &&
           write_pos_ >= buffer_start_ &&
           write_pos_ <= buffer_end_;
}

}
}

