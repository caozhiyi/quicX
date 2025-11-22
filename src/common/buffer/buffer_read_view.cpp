#include <cstring>
#include <cstdlib>

#include "common/log/log.h"
#include "common/buffer/buffer_read_view.h"

namespace quicx {
namespace common {

BufferReadView::BufferReadView() = default;

// Convenience constructor that delegates to Reset().
BufferReadView::BufferReadView(uint8_t* start, uint32_t len)
    : BufferReadView(start, start ? start + len : nullptr) {}

BufferReadView::BufferReadView(uint8_t* start, uint8_t* end)
    : BufferReadView() {
    Reset(start, end);
}

void BufferReadView::Reset(uint8_t* start, uint32_t len) {
    Reset(start, start ? start + len : nullptr);
}

void BufferReadView::Reset(uint8_t* start, uint8_t* end) {
    buffer_start_ = start;
    buffer_end_ = end;
    read_pos_ = start;

    if (!Valid()) {
        LOG_ERROR("span is invalid");
        buffer_start_ = nullptr;
        buffer_end_ = nullptr;
        read_pos_ = nullptr;
        return;
    }
}

uint32_t BufferReadView::ReadNotMovePt(uint8_t* data, uint32_t len) {
    if (data == nullptr) {
        LOG_ERROR("data buffer is nullptr");
        return 0;
    }
    return InnerRead(data, len, false);
}

uint32_t BufferReadView::MoveReadPt(uint32_t len) {
    if (!Valid()) {
        LOG_ERROR("span is invalid");
        return 0;
    }

    size_t size = buffer_end_ - read_pos_;
    if (static_cast<int32_t>(size) <= len) {
        read_pos_ = buffer_end_;
        return static_cast<uint32_t>(size);
    }
    read_pos_ += len;
    return static_cast<uint32_t>(len);
   
}

uint32_t BufferReadView::Read(uint8_t* data, uint32_t len) {
    if (data == nullptr) {
        LOG_ERROR("data buffer is nullptr");
        return 0;
    }
    return InnerRead(data, len, true);
}

void BufferReadView::VisitData(const std::function<bool(uint8_t*, uint32_t)>& visitor) {
    if (!visitor) {
        return;
    }
    if (!Valid()) {
        LOG_ERROR("span is invalid");
        return;
    }
    uint32_t readable = static_cast<uint32_t>(buffer_end_ - read_pos_);
    if (readable == 0) {
        return;
    }
    visitor(read_pos_, readable);
}

uint32_t BufferReadView::GetDataLength() {
    if (!Valid()) {
        LOG_ERROR("span is invalid");
        return 0;
    }
    return static_cast<uint32_t>(buffer_end_ - read_pos_);
}

uint32_t BufferReadView::GetDataLength() const {
    return const_cast<BufferReadView*>(this)->GetDataLength();
}

uint8_t* BufferReadView::GetData() const {
    if (!Valid()) {
        LOG_ERROR("span is invalid");
        return nullptr;
    }
    return read_pos_;
}

BufferSpan BufferReadView::GetReadableSpan() const {
    if (!Valid()) {
        LOG_ERROR("span is invalid");
        return BufferSpan();
    }
    return BufferSpan(read_pos_, buffer_end_);
}

void BufferReadView::Clear() {
    buffer_start_ = nullptr;
    buffer_end_ = nullptr;
    read_pos_ = nullptr;
}

uint32_t BufferReadView::InnerRead(uint8_t* data, uint32_t len, bool move_pt) {
    if (!Valid()) {
        LOG_ERROR("span is invalid");
        return 0;
    }

    size_t size = buffer_end_ - read_pos_;
    if (size <= len) {
        std::memcpy(data, read_pos_, size);
        if (move_pt) {
            read_pos_ = buffer_end_;
        }
        return static_cast<uint32_t>(size);
    }

    std::memcpy(data, read_pos_, len);
    if (move_pt) {
        read_pos_ += len;
    }
    return len;
}

bool BufferReadView::Valid() const {
    return buffer_start_ != nullptr &&
           buffer_end_ != nullptr &&
           buffer_start_ <= buffer_end_ &&
           read_pos_ >= buffer_start_ &&
           read_pos_ <= buffer_end_;
}

}
}

