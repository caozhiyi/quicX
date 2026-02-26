#include <cstring>
#include <vector>

#include "common/log/log.h"
#include "common/buffer/if_buffer.h"
#include "common/buffer/buffer_reader.h"

namespace quicx {
namespace common {

BufferReader::BufferReader() = default;

BufferReader::BufferReader(uint8_t* start, uint8_t* end) {
    Reset(start, end);
}

BufferReader::BufferReader(uint8_t* start, uint32_t len)
    : BufferReader(start, start ? start + len : nullptr) {}

BufferReader::BufferReader(std::shared_ptr<IBuffer> buffer) {
    Reset(buffer);
}

void BufferReader::Reset(uint8_t* start, uint32_t len) {
    Reset(start, start ? start + len : nullptr);
}

void BufferReader::Reset(uint8_t* start, uint8_t* end) {
    // Clear IBuffer mode state
    buffer_ = nullptr;
    read_offset_ = 0;
    is_contiguous_ = true;

    buffer_start_ = start;
    buffer_end_ = end;
    read_pos_ = start;

    if (start && end && start <= end) {
        return;
    }

    LOG_ERROR("BufferReader: invalid contiguous range");
    buffer_start_ = nullptr;
    buffer_end_ = nullptr;
    read_pos_ = nullptr;
}

void BufferReader::Reset(std::shared_ptr<IBuffer> buffer) {
    // Clear contiguous mode state
    read_pos_ = nullptr;
    buffer_start_ = nullptr;
    buffer_end_ = nullptr;

    buffer_ = buffer;
    read_offset_ = 0;
    is_contiguous_ = false;
}

// --------------- IBufferRead interface ---------------

uint32_t BufferReader::ReadNotMovePt(uint8_t* data, uint32_t len) {
    if (data == nullptr) {
        LOG_ERROR("data buffer is nullptr");
        return 0;
    }

    if (is_contiguous_) {
        return ContiguousRead(data, len, false);
    }

    // IBuffer mode
    if (!Valid()) {
        LOG_ERROR("BufferReader is invalid");
        return 0;
    }

    uint32_t available = buffer_->GetDataLength();
    if (available <= read_offset_) {
        return 0;
    }
    uint32_t remaining = available - read_offset_;
    uint32_t to_read = (len < remaining) ? len : remaining;
    if (to_read == 0) {
        return 0;
    }

    uint32_t current_offset = 0;
    uint32_t copied = 0;

    buffer_->VisitData([&](uint8_t* chunk_data, uint32_t chunk_len) -> bool {
        uint32_t chunk_start = current_offset;
        uint32_t chunk_end = current_offset + chunk_len;
        current_offset += chunk_len;

        uint32_t read_start = read_offset_;
        uint32_t read_end = read_offset_ + to_read;

        if (chunk_end <= read_start) {
            return true;
        }
        if (chunk_start >= read_end) {
            return false;
        }

        uint32_t overlap_start = std::max(chunk_start, read_start);
        uint32_t overlap_end = std::min(chunk_end, read_end);
        uint32_t overlap_len = overlap_end - overlap_start;

        uint32_t chunk_offset = overlap_start - chunk_start;
        uint32_t dest_offset = overlap_start - read_start;

        memcpy(data + dest_offset, chunk_data + chunk_offset, overlap_len);
        copied += overlap_len;

        return copied < to_read;
    });

    return copied;
}

uint32_t BufferReader::MoveReadPt(uint32_t len) {
    if (is_contiguous_) {
        if (!Valid()) {
            LOG_ERROR("BufferReader is invalid");
            return 0;
        }
        uint32_t size = static_cast<uint32_t>(buffer_end_ - read_pos_);
        if (size <= len) {
            read_pos_ = buffer_end_;
            return size;
        }
        read_pos_ += len;
        return len;
    }

    // IBuffer mode
    if (!Valid()) {
        LOG_ERROR("BufferReader is invalid");
        return 0;
    }
    uint32_t available = buffer_->GetDataLength();
    if (available <= read_offset_) {
        return 0;
    }
    uint32_t remaining = available - read_offset_;
    uint32_t to_move = (len < remaining) ? len : remaining;
    read_offset_ += to_move;
    return to_move;
}

uint32_t BufferReader::Read(uint8_t* data, uint32_t len) {
    if (data == nullptr) {
        LOG_ERROR("data buffer is nullptr");
        return 0;
    }

    if (is_contiguous_) {
        return ContiguousRead(data, len, true);
    }

    // IBuffer mode
    uint32_t read = ReadNotMovePt(data, len);
    if (read > 0) {
        read_offset_ += read;
    }
    return read;
}

void BufferReader::VisitData(const std::function<bool(uint8_t*, uint32_t)>& visitor) {
    if (!visitor || !Valid()) {
        return;
    }

    if (is_contiguous_) {
        uint32_t readable = static_cast<uint32_t>(buffer_end_ - read_pos_);
        if (readable > 0) {
            visitor(read_pos_, readable);
        }
        return;
    }

    // IBuffer mode: visit chunks, skipping the consumed offset
    uint32_t available = buffer_->GetDataLength();
    if (available <= read_offset_) {
        return;
    }

    uint32_t remaining = available - read_offset_;
    uint32_t current_offset = 0;
    uint32_t visited = 0;

    buffer_->VisitData([&](uint8_t* chunk_data, uint32_t chunk_len) -> bool {
        uint32_t chunk_start = current_offset;
        uint32_t chunk_end = current_offset + chunk_len;
        current_offset += chunk_len;

        if (chunk_end <= read_offset_) {
            return true;
        }

        uint32_t data_start = (chunk_start < read_offset_) ? read_offset_ - chunk_start : 0;
        uint32_t data_len = chunk_len - data_start;
        uint32_t to_visit = std::min(data_len, remaining - visited);

        bool cont = visitor(chunk_data + data_start, to_visit);
        visited += to_visit;

        return cont && visited < remaining;
    });
}

uint32_t BufferReader::GetDataLength() {
    if (is_contiguous_) {
        if (!Valid()) {
            return 0;
        }
        return static_cast<uint32_t>(buffer_end_ - read_pos_);
    }

    if (!Valid()) {
        return 0;
    }
    uint32_t available = buffer_->GetDataLength();
    if (available <= read_offset_) {
        return 0;
    }
    return available - read_offset_;
}

uint32_t BufferReader::GetDataLength() const {
    return const_cast<BufferReader*>(this)->GetDataLength();
}

void BufferReader::Clear() {
    if (is_contiguous_) {
        buffer_start_ = nullptr;
        buffer_end_ = nullptr;
        read_pos_ = nullptr;
    } else {
        buffer_ = nullptr;
        read_offset_ = 0;
    }
}

// --------------- Extended methods ---------------

void BufferReader::Sync() {
    if (is_contiguous_ || !Valid() || read_offset_ == 0) {
        return;
    }
    buffer_->MoveReadPt(read_offset_);
    read_offset_ = 0;
}

uint32_t BufferReader::GetReadOffset() const {
    if (is_contiguous_) {
        if (!read_pos_ || !buffer_start_) {
            return 0;
        }
        return static_cast<uint32_t>(read_pos_ - buffer_start_);
    }
    return read_offset_;
}

uint8_t* BufferReader::GetData() const {
    if (!is_contiguous_ || !Valid()) {
        return nullptr;
    }
    return read_pos_;
}

BufferSpan BufferReader::GetReadableSpan() const {
    if (!is_contiguous_ || !Valid()) {
        return BufferSpan();
    }
    return BufferSpan(read_pos_, buffer_end_);
}

bool BufferReader::Valid() const {
    if (is_contiguous_) {
        return buffer_start_ != nullptr &&
               buffer_end_ != nullptr &&
               buffer_start_ <= buffer_end_ &&
               read_pos_ >= buffer_start_ &&
               read_pos_ <= buffer_end_;
    }
    return buffer_ != nullptr;
}

// --------------- Private ---------------

uint32_t BufferReader::ContiguousRead(uint8_t* data, uint32_t len, bool move_pt) {
    if (!Valid()) {
        LOG_ERROR("BufferReader is invalid");
        return 0;
    }

    uint32_t size = static_cast<uint32_t>(buffer_end_ - read_pos_);
    if (size <= len) {
        std::memcpy(data, read_pos_, size);
        if (move_pt) {
            read_pos_ = buffer_end_;
        }
        return size;
    }

    std::memcpy(data, read_pos_, len);
    if (move_pt) {
        read_pos_ += len;
    }
    return len;
}

}  // namespace common
}  // namespace quicx
