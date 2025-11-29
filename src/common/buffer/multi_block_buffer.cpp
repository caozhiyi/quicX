#include <cstring>
#include <algorithm>

#include "common/log/log.h"
#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_chunk.h"
#include "common/buffer/multi_block_buffer.h"

namespace quicx {
namespace common {

MultiBlockBuffer::MultiBlockBuffer(std::shared_ptr<BlockMemoryPool> pool, bool pre_allocate):
    pool_(std::move(pool)) {
    // Pre-allocate first chunk_ to avoid empty buffer issues
    if (pool_ && pre_allocate) {
        EnsureWritableChunk();
    }
    total_data_length_ = 0;
}

// Clears all readable and writable spans while keeping capacity bookkeeping in
// sync.
void MultiBlockBuffer::Reset() {
    Clear();
    total_data_length_ = 0;
}

void MultiBlockBuffer::Clear() {
    chunks_.clear();
    total_data_length_ = 0;
}

// Copy data out of the buffer without mutating internal state. Useful for
// peeking at headers or performing checksum calculations.
uint32_t MultiBlockBuffer::ReadNotMovePt(uint8_t* data, uint32_t len) {
    if (data == nullptr || len == 0) {
        if (!data) {
            LOG_ERROR("data buffer is nullptr");
        }
        return 0;
    }

    uint32_t copied = 0;
    for (const auto& state : chunks_) {
        if (!state.chunk_ || !state.chunk_->Valid() || state.Readable() == 0) {
            continue;
        }

        auto* start = state.DataStart();
        if (!start) {
            continue;
        }

        uint32_t available = state.Readable();
        uint32_t to_copy = std::min(len - copied, available);
        std::memcpy(data + copied, start, to_copy);
        copied += to_copy;
        if (copied >= len) {
            break;
        }
    }
    return copied;
}

uint32_t MultiBlockBuffer::Read(uint8_t* data, uint32_t len) {
    if (data == nullptr || len == 0) {
        if (!data) {
            LOG_ERROR("data buffer is nullptr");
        }
        return 0;
    }

    uint32_t copied = 0;
    while (!chunks_.empty() && copied < len) {
        auto& state = chunks_.front();
        if (!state.chunk_ || !state.chunk_->Valid()) {
            chunks_.pop_front();
            continue;
        }

        uint32_t available = state.Readable();
        if (available == 0) {
            chunks_.pop_front();
            continue;
        }

        uint32_t to_copy = std::min(len - copied, available);
        std::memcpy(data + copied, state.read_pos_, to_copy);

        state.read_pos_ += to_copy;
        copied += to_copy;
        total_data_length_ -= to_copy;

        if (state.read_pos_ >= state.write_pos_) {
            chunks_.pop_front();
        }
    }
    return copied;
}

uint32_t MultiBlockBuffer::MoveReadPt(uint32_t len) {
    if (len == 0 || chunks_.empty()) {
        return 0;
    }

    uint32_t moved = 0;
    while (!chunks_.empty() && moved < len) {
        auto& state = chunks_.front();
        if (!state.chunk_ || !state.chunk_->Valid()) {
            chunks_.pop_front();
            continue;
        }

        uint32_t available = state.Readable();
        if (available == 0) {
            chunks_.pop_front();
            continue;
        }

        uint32_t remaining = len - moved;
        uint32_t step = std::min(remaining, available);
        state.read_pos_ += step;
        moved += step;
        total_data_length_ -= step;

        if (state.read_pos_ >= state.write_pos_) {
            chunks_.pop_front();
        }
    }

    return moved;
}

void MultiBlockBuffer::VisitData(const std::function<bool(uint8_t*, uint32_t)>& visitor) {
    if (!visitor) {
        return;
    }

    for (const auto& state : chunks_) {
        if (!state.chunk_ || !state.chunk_->Valid()) {
            continue;
        }

        uint32_t readable = state.Readable();
        if (readable == 0) {
            continue;
        }

        if (!visitor(state.read_pos_, readable)) {
            break;
        }
    }
}

uint32_t MultiBlockBuffer::GetDataLength() {
    return total_data_length_;
}

std::shared_ptr<IBuffer> MultiBlockBuffer::CloneReadable(uint32_t length, bool move_write_pt) {
    if (GetDataLength() < length) {
        LOG_ERROR("insufficient data: available=%u, requested=%u", GetDataLength(), length);
        return nullptr;
    }

    // Create a new MultiBlockBuffer with the same pool (shallow copy)
    auto clone = std::make_shared<MultiBlockBuffer>(pool_, false);

    // Share the underlying chunks (shallow copy)
    uint32_t remaining = length;
    for (const auto& state : chunks_) {
        if (remaining == 0) {
            break;
        }

        uint32_t available = state.Readable();
        if (available == 0) {
            continue;
        }

        uint32_t to_copy = std::min(remaining, available);

        // Create a new ChunkState that shares the same chunk_ but with adjusted offsets
        ChunkState clone_state;
        clone_state.chunk_ = state.chunk_;  // Shallow copy: share the same chunk_
        clone_state.read_pos_ = state.read_pos_;
        clone_state.write_pos_ = state.read_pos_ + to_copy;

        clone->chunks_.push_back(clone_state);
        clone->total_data_length_ += to_copy;

        remaining -= to_copy;
    }

    // Advance this buffer's read pointer
    if (move_write_pt) {
        MoveReadPt(length);
    }

    return clone;
}

BufferReadView MultiBlockBuffer::GetReadView() const {
    for (const auto& state : chunks_) {
        if (!state.chunk_ || !state.chunk_->Valid()) {
            continue;
        }
        uint32_t readable = state.Readable();
        if (readable == 0) {
            continue;
        }
        return BufferReadView(state.read_pos_, state.write_pos_);
    }
    return BufferReadView();
}

BufferSpan MultiBlockBuffer::GetReadableSpan() const {
    for (const auto& state : chunks_) {
        if (!state.chunk_ || !state.chunk_->Valid()) {
            continue;
        }
        uint32_t readable = state.Readable();
        if (readable == 0) {
            continue;
        }
        return BufferSpan(state.read_pos_, state.write_pos_);
    }
    return BufferSpan();
}

SharedBufferSpan MultiBlockBuffer::GetSharedReadableSpan(uint32_t length, bool must_fill_length) const {
    uint32_t available = total_data_length_;

    // length == 0 means all available data
    if (length == 0) {
        length = available;
    }

    if (must_fill_length && length > available) {
        LOG_WARN("not enough data: have %u need %u", available, length);
        return SharedBufferSpan();
    }

    // Try to return from first chunk_ if possible
    if (chunks_.empty()) {
        LOG_ERROR("no chunks available");
        return SharedBufferSpan();
    }

    // Single chunk_ case
    if (chunks_.size() == 1) {
        const auto& state = chunks_.front();
        uint32_t readable = state.Readable();
        if (readable == 0 || !state.chunk_ || !state.chunk_->Valid()) {
            LOG_ERROR("first chunk is invalid");
            return SharedBufferSpan();
        }
        // Return up to min(length, readable)
        uint32_t actual_len = std::min(length, readable);
        return SharedBufferSpan(state.chunk_, state.read_pos_, state.read_pos_ + actual_len);
    }

    // Multiple chunks case: check if first chunk_ has enough data
    const auto& first = chunks_.front();
    if (first.chunk_ && first.chunk_->Valid() && first.Readable() >= length) {
        return SharedBufferSpan(first.chunk_, first.read_pos_, first.read_pos_ + length);
    }

    // Fallback: Need to merge multiple chunks into a new chunk_ (deep copy)
    if (!pool_) {
        LOG_ERROR("no memory pool available for merging chunks");
        return SharedBufferSpan();
    }

    auto new_chunk = std::make_shared<BufferChunk>(pool_);
    if (!new_chunk || !new_chunk->Valid()) {
        LOG_ERROR("failed to allocate new chunk_ for merging");
        return SharedBufferSpan();
    }

    uint32_t capacity = new_chunk->GetLength();
    if (capacity == 0 || length > capacity) {
        LOG_ERROR("new chunk has not enough capacity: have %u need %u", capacity, length);
        return SharedBufferSpan();
    }

    // Copy data from multiple chunks
    uint8_t* dest = new_chunk->GetData();
    uint32_t remaining = length;
    for (const auto& state : chunks_) {
        if (!state.chunk_ || !state.chunk_->Valid()) {
            continue;
        }
        uint32_t readable = state.Readable();
        if (readable == 0) {
            continue;
        }
        uint32_t copy_len = std::min(readable, remaining);
        std::memcpy(dest, state.read_pos_, copy_len);
        dest += copy_len;
        remaining -= copy_len;
        if (remaining == 0) {
            break;
        }
    }

    uint32_t copied = length - remaining;
    return SharedBufferSpan(new_chunk, new_chunk->GetData(), new_chunk->GetData() + copied);
}

std::string MultiBlockBuffer::GetDataAsString() {
    std::string data;
    data.reserve(GetDataLength());
    VisitData([&](uint8_t* d, uint32_t len) {
        data.append(reinterpret_cast<char*>(d), len);
        return true;
    });
    return data;
}

void MultiBlockBuffer::VisitDataSpans(const std::function<bool(SharedBufferSpan&)>& visitor) {
    if (!visitor) {
        return;
    }
    for (const auto& state : chunks_) {
        if (!state.chunk_ || !state.chunk_->Valid()) {
            continue;
        }
        uint32_t readable = state.Readable();
        if (readable == 0) {
            continue;
        }
        SharedBufferSpan span(state.chunk_, state.read_pos_, state.write_pos_);
        if (!visitor(span)) {
            break;
        }
    }
}

uint32_t MultiBlockBuffer::Write(const uint8_t* data, uint32_t len) {
    if (data == nullptr || len == 0) {
        if (!data) {
            LOG_ERROR("data buffer is nullptr");
        }
        return 0;
    }

    uint32_t written = 0;
    while (written < len) {
        if (!EnsureWritableChunk()) {
            break;
        }

        auto& state = chunks_.back();
        if (!state.chunk_ || !state.chunk_->Valid()) {
            chunks_.pop_back();
            continue;
        }

        uint32_t capacity = state.Writable();
        if (capacity == 0) {
            continue;
        }
        uint32_t to_copy = std::min<uint32_t>(len - written, capacity);
        std::memcpy(state.write_pos_, data + written, to_copy);
        state.write_pos_ += to_copy;
        total_data_length_ += to_copy;
        written += to_copy;
    }

    return written;
}

// Append an entire span to the buffer.
uint32_t MultiBlockBuffer::Write(const SharedBufferSpan& span) {
    return Write(span, span.GetLength());
}

uint32_t MultiBlockBuffer::Write(std::shared_ptr<IBuffer> buffer) {
    if (!buffer) {
        return 0;
    }

    uint32_t written = 0;
    buffer->VisitDataSpans([&](SharedBufferSpan& span) {
        if (!span.Valid()) {
            return true;
        }

        uint32_t chunk_written = Write(span);
        written += chunk_written;
        return true;
    });

    return written;
}

// Append a span while exposing at most data_len bytes to readers. The span
// keeps the underlying chunk_ alive so we simply track offsets.
uint32_t MultiBlockBuffer::Write(const SharedBufferSpan& span, uint32_t data_len) {
    if (!span.Valid()) {
        LOG_ERROR("span is invalid");
        return 0;
    }

    auto chunk_ = span.GetChunk();
    if (!chunk_) {
        LOG_ERROR("span has no chunk_");
        return 0;
    }

    uint32_t span_len = span.GetLength();
    if (data_len > span_len) {
        data_len = span_len;
    }

    // if the last chunk has enough writable space, write to it
    if (!chunks_.empty()) {
        auto& back = chunks_.back();
        uint32_t capacity = back.Writable();
        if (capacity >= data_len) {
            return Write(span.GetStart(), data_len);
        }
    }

    // if the last chunk does not have enough writable space, push the chunk
    ChunkState state;
    state.chunk_ = chunk_;
    state.read_pos_ = span.GetStart();
    state.write_pos_ = span.GetStart() + data_len;

    chunks_.push_back(std::move(state));
    total_data_length_ += data_len;

    return data_len;
}

uint32_t MultiBlockBuffer::GetFreeLength() {
    if (chunks_.empty()) {
        return 0;
    }
    auto& back = chunks_.back();
    if (!back.chunk_ || !back.chunk_->Valid()) {
        return 0;
    }
    return back.Writable();
}

uint32_t MultiBlockBuffer::MoveWritePt(uint32_t len) {
    if (len == 0) {
        return 0;
    }

    uint32_t moved = 0;
    while (moved < len) {
        // Ensure we have a writable chunk_ (same logic as Write method)
        if (!EnsureWritableChunk()) {
            break;
        }

        auto& state = chunks_.back();
        if (!state.chunk_ || !state.chunk_->Valid()) {
            chunks_.pop_back();
            continue;
        }

        uint32_t capacity = state.Writable();
        if (capacity == 0) {
            // Current chunk is full, EnsureWritableChunk should have created a new one
            // Continue to next iteration to use the new chunk
            continue;
        }

        uint32_t remaining = len - moved;
        uint32_t step = std::min(remaining, capacity);
        state.write_pos_ += step;
        moved += step;
        total_data_length_ += step;
    }
    return moved;
}

uint32_t MultiBlockBuffer::Write(std::shared_ptr<IBufferRead> buffer, uint32_t data_len) {
    if (!buffer || data_len == 0) {
        return 0;
    }

    uint32_t written = 0;
    buffer->VisitData([&](uint8_t* data, uint32_t len) {
        if (len == 0) {
            return true;
        }
        uint32_t to_copy = std::min(len, data_len - written);
        uint32_t chunk_written = Write(data, to_copy);
        written += chunk_written;
        if (written >= data_len) {
            return false;
        }
        return true;
    });
    return written;
}

BufferSpan MultiBlockBuffer::GetWritableSpan() {
    // First attempt: ensure we have a writable chunk_
    if (!EnsureWritableChunk()) {
        return BufferSpan();
    }

    auto& back = chunks_.back();
    if (!back.chunk_ || !back.chunk_->Valid()) {
        return BufferSpan();
    }
    uint32_t capacity = back.Writable();
    return BufferSpan(back.write_pos_, back.write_pos_ + capacity);
}

BufferSpan MultiBlockBuffer::GetWritableSpan(uint32_t expected_length) {
    if (expected_length == 0) {
        return BufferSpan();
    }
    auto chunk = EnsureWritableChunk();
    if (!chunk) {
        return BufferSpan();
    }

    auto& back = chunks_.back();
    if (!back.chunk_ || !back.chunk_->Valid()) {
        LOG_ERROR("last chunk is invalid");
        return BufferSpan();
    }
    uint32_t free_len = back.Writable();
    if (free_len < expected_length) {
        LOG_ERROR("last chunk has not enough writable space: have %u need %u", free_len, expected_length);
        return BufferSpan();
    }
    return BufferSpan(back.write_pos_, back.write_pos_ + expected_length);
}

std::shared_ptr<IBufferChunk> MultiBlockBuffer::GetChunk() const {
    LOG_ERROR("not implemented, should not be called");
    return nullptr;
}

uint32_t MultiBlockBuffer::GetChunkCount() const {
    return chunks_.size();
}
uint32_t MultiBlockBuffer::Write(std::shared_ptr<IBufferRead> buffer) {
    if (!buffer) {
        return 0;
    }

    uint32_t written = 0;
    buffer->VisitData([&](uint8_t* data, uint32_t len) {
        if (len == 0) {
            return true;
        }
        written += Write(data, len);
        return true;
    });
    return written;
}

bool MultiBlockBuffer::Empty() const {
    return total_data_length_ == 0;
}

std::shared_ptr<IBufferChunk> MultiBlockBuffer::EnsureWritableChunk() {
    if (!chunks_.empty()) {
        auto& back = chunks_.back();
        if (back.chunk_ && back.chunk_->Valid()) {
            if (back.Writable() > 0) {
                return back.chunk_;
            }
        }
    }

    auto chunk = std::make_shared<BufferChunk>(pool_);
    if (!chunk || !chunk->Valid()) {
        LOG_ERROR("failed to allocate new chunk");
        return nullptr;
    }

    ChunkState state;
    state.chunk_ = chunk;
    state.read_pos_ = chunk->GetData();
    state.write_pos_ = chunk->GetData();
    chunks_.push_back(std::move(state));

    return chunk;
}

}  // namespace common
}  // namespace quicx
