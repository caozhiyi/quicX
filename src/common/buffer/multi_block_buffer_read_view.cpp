#include "common/buffer/multi_block_buffer_read_view.h"
#include "common/log/log.h"
#include <cstring>
#include <vector>

namespace quicx {
namespace common {

MultiBlockBufferReadView::MultiBlockBufferReadView():
    buffer_(nullptr),
    read_offset_(0) {
}

MultiBlockBufferReadView::MultiBlockBufferReadView(std::shared_ptr<IBuffer> buffer):
    buffer_(buffer),
    read_offset_(0) {
}

void MultiBlockBufferReadView::Reset(std::shared_ptr<IBuffer> buffer) {
    buffer_ = buffer;
    read_offset_ = 0;
}

uint32_t MultiBlockBufferReadView::ReadNotMovePt(uint8_t* data, uint32_t len) {
    if (!Valid()) {
        LOG_ERROR("MultiBlockBufferReadView is invalid");
        return 0;
    }
    
    if (data == nullptr) {
        LOG_ERROR("data buffer is nullptr");
        return 0;
    }
    
    // Check available data
    uint32_t available = buffer_->GetDataLength();
    if (available <= read_offset_) {
        return 0;
    }
    
    uint32_t remaining = available - read_offset_;
    uint32_t to_read = (len < remaining) ? len : remaining;
    
    if (to_read == 0) {
        return 0;
    }
    
    // Read from the buffer starting at read_offset_
    // We need to read read_offset_ + to_read bytes, then skip the first read_offset_ bytes
    uint32_t total_to_read = read_offset_ + to_read;
    
    // Use VisitData to read data across multiple blocks
    // This is more reliable than ReadNotMovePt for multi-block buffers
    uint32_t copied = 0;
    uint32_t skip_bytes = read_offset_;
    
    buffer_->VisitData([&](uint8_t* chunk_data, uint32_t chunk_len) -> bool {
        if (skip_bytes > 0) {
            if (skip_bytes >= chunk_len) {
                skip_bytes -= chunk_len;
                return true;  // Continue to next chunk
            }
            // Skip partial chunk
            uint32_t to_skip = skip_bytes;
            chunk_data += to_skip;
            chunk_len -= to_skip;
            skip_bytes = 0;
        }
        
        // Copy from this chunk
        uint32_t to_copy = (chunk_len < to_read - copied) ? chunk_len : (to_read - copied);
        memcpy(data + copied, chunk_data, to_copy);
        copied += to_copy;
        
        return copied < to_read;  // Continue if we need more
    });
    
    return copied;
}

uint32_t MultiBlockBufferReadView::MoveReadPt(uint32_t len) {
    if (!Valid()) {
        LOG_ERROR("MultiBlockBufferReadView is invalid");
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

uint32_t MultiBlockBufferReadView::Read(uint8_t* data, uint32_t len) {
    if (!Valid()) {
        LOG_ERROR("MultiBlockBufferReadView is invalid");
        return 0;
    }
    
    uint32_t read = ReadNotMovePt(data, len);
    if (read > 0) {
        read_offset_ += read;
    }
    return read;
}

void MultiBlockBufferReadView::VisitData(const std::function<bool(uint8_t*, uint32_t)>& visitor) {
    if (!Valid()) {
        LOG_ERROR("MultiBlockBufferReadView is invalid");
        return;
    }
    
    if (!visitor) {
        return;
    }
    
    uint32_t available = buffer_->GetDataLength();
    if (available <= read_offset_) {
        return;
    }
    
    uint32_t remaining = available - read_offset_;
    if (remaining == 0) {
        return;
    }
    
    // Read all remaining data into a temporary buffer and visit it
    // For efficiency, use stack buffer for small data, heap for large data
    uint8_t stack_buf[4096];  // 4KB stack buffer
    uint8_t* visit_buf = nullptr;
    std::vector<uint8_t> heap_buf;
    
    if (remaining <= sizeof(stack_buf)) {
        visit_buf = stack_buf;
    } else {
        heap_buf.resize(remaining);
        visit_buf = heap_buf.data();
    }
    
    // Read the data starting from read_offset_
    uint32_t total_to_read = read_offset_ + remaining;
    uint8_t* read_buf = nullptr;
    std::vector<uint8_t> read_heap_buf;
    
    if (total_to_read <= sizeof(stack_buf)) {
        read_buf = stack_buf;
    } else {
        read_heap_buf.resize(total_to_read);
        read_buf = read_heap_buf.data();
    }
    
    // Read from buffer's current position
    uint32_t read_total = buffer_->ReadNotMovePt(read_buf, total_to_read);
    if (read_total < total_to_read) {
        // Not enough data
        return;
    }
    
    // Copy the data we need (skip the first read_offset_ bytes)
    if (visit_buf != read_buf + read_offset_) {
        memcpy(visit_buf, read_buf + read_offset_, remaining);
    }
    
    // Visit the data
    visitor(visit_buf, remaining);
}

uint32_t MultiBlockBufferReadView::GetDataLength() {
    if (!Valid()) {
        return 0;
    }
    
    uint32_t available = buffer_->GetDataLength();
    if (available <= read_offset_) {
        return 0;
    }
    
    return available - read_offset_;
}

uint32_t MultiBlockBufferReadView::GetDataLength() const {
    return const_cast<MultiBlockBufferReadView*>(this)->GetDataLength();
}

void MultiBlockBufferReadView::Clear() {
    buffer_ = nullptr;
    read_offset_ = 0;
}

void MultiBlockBufferReadView::Sync() {
    if (!Valid() || read_offset_ == 0) {
        return;
    }
    
    // Move the underlying buffer's read pointer forward by read_offset_
    buffer_->MoveReadPt(read_offset_);
    read_offset_ = 0;
}

uint32_t MultiBlockBufferReadView::GetReadOffset() const {
    return read_offset_;
}

bool MultiBlockBufferReadView::Valid() const {
    return buffer_ != nullptr;
}

}  // namespace common
}  // namespace quicx

