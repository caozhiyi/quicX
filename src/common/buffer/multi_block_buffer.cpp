#include <algorithm>
#include <cstring>
#include <limits>

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_chunk.h"
#include "common/log/log.h"
#include "common/buffer/multi_block_buffer.h"


namespace quicx {
namespace common {

MultiBlockBuffer::MultiBlockBuffer(std::shared_ptr<BlockMemoryPool> pool):
    pool_(std::move(pool)) {
}

// Clears all readable and writable spans while keeping capacity bookkeeping in
// sync.
void MultiBlockBuffer::Reset() {
    Clear();
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
        if (!state.span.Valid() || state.Readable() == 0) {
            continue;
        }

        auto* start = state.DataStart();
        if (!start) {
            continue;
        }

        uint32_t available = state.Readable();
        uint32_t to_copy = std::min(len - copied, available);
        std::memcpy(data + copied, start + state.read_offset, to_copy);
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
        if (!state.span.Valid()) {
            chunks_.pop_front();
            continue;
        }

        auto* start = state.DataStart();
        if (!start) {
            chunks_.pop_front();
            continue;
        }

        uint32_t available = state.Readable();
        if (available == 0) {
            chunks_.pop_front();
            continue;
        }

        uint32_t to_copy = std::min(len - copied, available);
        std::memcpy(data + copied, start + state.read_offset, to_copy);

        state.read_offset += to_copy;
        copied += to_copy;
        total_data_length_ -= to_copy;

        if (state.read_offset >= state.write_limit) {
            chunks_.pop_front();
        }
    }
    return copied;
}

uint32_t MultiBlockBuffer::MoveReadPt(int32_t len) {
    if (len == 0 || chunks_.empty()) {
        return 0;
    }

    uint32_t moved = 0;

    if (len > 0) {
        while (len > 0 && !chunks_.empty()) {
            auto& state = chunks_.front();
            if (!state.span.Valid()) {
                chunks_.pop_front();
                continue;
            }

            uint32_t available = state.Readable();
            if (available == 0) {
                chunks_.pop_front();
                continue;
            }

            uint32_t step = std::min<uint32_t>(len, available);
            state.read_offset += step;
            len -= static_cast<int32_t>(step);
            moved += step;
            total_data_length_ -= step;

            if (state.read_offset >= state.write_limit) {
                chunks_.pop_front();
            }
        }

    } else {
        uint32_t rewind = static_cast<uint32_t>(-len);
        while (rewind > 0 && !chunks_.empty()) {
            auto& state = chunks_.front();
            if (!state.span.Valid()) {
                chunks_.pop_front();
                continue;
            }

            uint32_t available = state.read_offset;
            if (available == 0) {
                break;
            }

            uint32_t step = std::min<uint32_t>(rewind, available);
            state.read_offset -= step;
            rewind -= step;
            moved += step;
            total_data_length_ += step;
        }
    }

    return moved;
}

// Append an entire span to the buffer.
uint32_t MultiBlockBuffer::Write(const SharedBufferSpan& span) {
    return Write(span, span.GetLength());
}

// Append a span while exposing at most data_len bytes to readers. The span
// keeps the underlying chunk alive so we simply track offsets.
uint32_t MultiBlockBuffer::Write(const SharedBufferSpan& span, uint32_t data_len) {
    if (!span.Valid()) {
        LOG_ERROR("span is invalid");
        return 0;
    }

    uint32_t span_len = span.GetLength();
    if (data_len > span_len) {
        data_len = span_len;
    }

    ChunkState state;
    state.span = span;
    state.read_offset = 0;
    state.write_limit = data_len;

    chunks_.push_back(std::move(state));
    total_data_length_ += data_len;
    return data_len;
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
        auto chunk = state.span.GetChunk();
        if (!chunk) {
            chunks_.pop_back();
            continue;
        }

        uint32_t capacity = chunk->GetLength();
        uint32_t free_space = capacity - state.write_limit;
        if (free_space == 0) {
            // Shouldn't happen because EnsureWritableChunk guarantees space,
            // but handle defensively.
            chunks_.push_back(state);
            continue;
        }

        uint32_t to_copy = std::min<uint32_t>(len - written, free_space);
        std::memcpy(chunk->GetData() + state.write_limit, data + written, to_copy);
        state.write_limit += to_copy;
        total_data_length_ += to_copy;
        written += to_copy;
    }
    return written;
}

uint32_t MultiBlockBuffer::Write(std::shared_ptr<IBufferRead> buffer) {
    if (!buffer) {
        return 0;
    }

    uint32_t written = 0;
    buffer->VisitData([&](uint8_t* data, uint32_t len) {
        if (len == 0) {
            return;
        }
        written += Write(data, len);
    });
    buffer->MoveReadPt(static_cast<int32_t>(written));
    return written;
}

uint32_t MultiBlockBuffer::Write(std::shared_ptr<IBuffer> buffer) {
    if (!buffer) {
        return 0;
    }
    if (!buffer) {
        return 0;
    }

    uint32_t remaining = GetDataLength();
    uint32_t written = 0;
    buffer->VisitData([&](uint8_t* data, uint32_t len) {
        if (remaining == 0 || len == 0) {
            return;
        }
        uint32_t to_copy = std::min(len, remaining);
        uint32_t chunk_written = Write(data, to_copy);
        written += chunk_written;
        if (chunk_written >= remaining) {
            remaining = 0;
        } else {
            remaining -= chunk_written;
        }
    });
    uint32_t consumed = std::min<uint32_t>(written, GetDataLength());
    buffer->MoveReadPt(static_cast<int32_t>(consumed));
    return consumed;
}

uint32_t MultiBlockBuffer::Write(std::shared_ptr<IBufferRead> buffer, uint32_t data_len) {
    if (!buffer || data_len == 0) {
        return 0;
    }

    uint32_t remaining = data_len;
    uint32_t written = 0;
    buffer->VisitData([&](uint8_t* data, uint32_t len) {
        if (remaining == 0 || len == 0) {
            return;
        }
        uint32_t to_copy = std::min(len, remaining);
        uint32_t chunk_written = Write(data, to_copy);
        written += chunk_written;
        if (chunk_written >= remaining) {
            remaining = 0;
        } else {
            remaining -= chunk_written;
        }
    });
    uint32_t consumed = std::min<uint32_t>(written, data_len);
    buffer->MoveReadPt(static_cast<int32_t>(consumed));
    return consumed;
}

uint32_t MultiBlockBuffer::MoveWritePt(int32_t len) {
    if (len == 0) {
        return 0;
    }

    uint32_t moved = 0;

    if (len > 0) {
        while (len > 0 && !chunks_.empty()) {
            auto& state = chunks_.back();
            if (!state.span.Valid()) {
                chunks_.pop_back();
                continue;
            }

            auto chunk = state.span.GetChunk();
            if (!chunk) {
                chunks_.pop_back();
                continue;
            }

            uint32_t capacity = state.span.GetLength();
            if (state.write_limit >= capacity) {
                break;
            }

            uint32_t free_space = capacity - state.write_limit;
            uint32_t step = std::min<uint32_t>(len, free_space);
            state.write_limit += step;
            len -= static_cast<int32_t>(step);
            moved += step;
            total_data_length_ += step;
        }
        return moved;
    }

    uint32_t to_rewind = static_cast<uint32_t>(-len);

    while (to_rewind > 0 && !chunks_.empty()) {
        auto& state = chunks_.back();
        if (!state.span.Valid()) {
            chunks_.pop_back();
            continue;
        }

        if (state.write_limit <= state.read_offset) {
            chunks_.pop_back();
            continue;
        }

        uint32_t writable_data = state.write_limit - state.read_offset;
        uint32_t step = std::min(to_rewind, writable_data);
        state.write_limit -= step;
        to_rewind -= step;
        moved += step;
        if (total_data_length_ >= step) {
            total_data_length_ -= step;
        } else {
            total_data_length_ = 0;
        }

        if (state.write_limit <= state.read_offset) {
            chunks_.pop_back();
        }
    }

    return moved;
}

void MultiBlockBuffer::VisitData(const std::function<void(uint8_t*, uint32_t)>& visitor) {
    if (!visitor) {
        return;
    }

    for (const auto& state : chunks_) {
        if (!state.span.Valid()) {
            continue;
        }

        uint32_t readable = state.Readable();
        if (readable == 0) {
            continue;
        }

        uint8_t* start = state.DataStart();
        if (!start) {
            continue;
        }
        visitor(start + state.read_offset, readable);
    }
}

void MultiBlockBuffer::VisitDataSpans(const std::function<void(SharedBufferSpan&)>& visitor) {
    if (!visitor) {
        return;
    }
    for (const auto& state : chunks_) {
        if (!state.span.Valid()) {
            continue;
        }
        uint32_t readable = state.Readable();
        if (readable == 0) {
            continue;
        }
        auto chunk = state.span.GetChunk();
        if (!chunk) {
            continue;
        }
        SharedBufferSpan span(chunk,
                              state.DataStart() + state.read_offset,
                              state.DataStart() + state.read_offset + readable);
        visitor(span);
    }
}

uint32_t MultiBlockBuffer::GetDataLength() {
    constexpr uint64_t max_value = std::numeric_limits<uint32_t>::max();
    return static_cast<uint32_t>(std::min(total_data_length_, max_value));
}

uint32_t MultiBlockBuffer::GetFreeLength() {
    uint64_t total_free = 0;
    for (const auto& state : chunks_) {
        if (!state.span.Valid()) {
            continue;
        }
        auto chunk = state.span.GetChunk();
        if (!chunk) {
            continue;
        }
        uint32_t chunk_length = state.span.GetLength();
        if (state.write_limit < chunk_length) {
            total_free += chunk_length - state.write_limit;
        }
    }
    constexpr uint64_t max_value = std::numeric_limits<uint32_t>::max();
    return static_cast<uint32_t>(std::min(total_free, max_value));
}

BufferReadView MultiBlockBuffer::GetReadView() const {
    for (const auto& state : chunks_) {
        if (!state.span.Valid()) {
            continue;
        }
        uint32_t readable = state.Readable();
        if (readable == 0) {
            continue;
        }
        return BufferReadView(state.DataStart() + state.read_offset, readable);
    }
    return BufferReadView();
}

BufferSpan MultiBlockBuffer::GetReadableSpan() const {
    for (const auto& state : chunks_) {
        if (!state.span.Valid()) {
            continue;
        }
        uint32_t readable = state.Readable();
        if (readable == 0) {
            continue;
        }
        return BufferSpan(state.DataStart() + state.read_offset,
                          state.DataStart() + state.read_offset + readable);
    }
    return BufferSpan();
}

std::string MultiBlockBuffer::GetDataAsString() {
    std::string data;
    data.reserve(GetDataLength());
    VisitData([&](uint8_t* d, uint32_t len) {
        data.append(reinterpret_cast<char*>(d), len);
    });
    return data;
}

BufferSpan MultiBlockBuffer::GetWritableSpan() {
    if (!EnsureWritableChunk()) {
        return BufferSpan();
    }
    auto& back = chunks_.back();
    if (!back.span.Valid()) {
        return BufferSpan();
    }
    auto chunk = back.span.GetChunk();
    if (!chunk) {
        return BufferSpan();
    }
    uint32_t capacity = chunk->GetLength();
    if (back.write_limit >= capacity) {
        return BufferSpan();
    }
    return BufferSpan(chunk->GetData() + back.write_limit,
                      chunk->GetData() + capacity);
}

BufferSpan MultiBlockBuffer::GetWritableSpan(uint32_t expected_length) {
    if (expected_length == 0) {
        return BufferSpan();
    }
    if (!EnsureWritableChunk(expected_length)) {
        return BufferSpan();
    }

    auto& back = chunks_.back();
    if (!back.span.Valid()) {
        return BufferSpan();
    }
    auto chunk = back.span.GetChunk();
    if (!chunk) {
        return BufferSpan();
    }
    uint32_t capacity = chunk->GetLength();
    if (capacity <= back.write_limit) {
        return BufferSpan();
    }
    uint32_t free_len = capacity - back.write_limit;
    if (free_len < expected_length) {
        return BufferSpan();
    }
    return BufferSpan(chunk->GetData() + back.write_limit,
                      chunk->GetData() + back.write_limit + expected_length);
}

SharedBufferSpan MultiBlockBuffer::GetSharedReadableSpan() const {
    for (const auto& state : chunks_) {
        if (!state.span.Valid()) {
            continue;
        }
        uint32_t readable = state.Readable();
        if (readable == 0) {
            continue;
        }
        return SharedBufferSpan(state.span.GetChunk(),
                                state.DataStart() + state.read_offset,
                                state.DataStart() + state.read_offset + readable);
    }
    return SharedBufferSpan();
}

SharedBufferSpan MultiBlockBuffer::GetSharedReadableSpan(uint32_t length) const {
    return GetSharedReadableSpan(length, false);
}

SharedBufferSpan MultiBlockBuffer::GetSharedReadableSpan(uint32_t length, bool must_fill_length) const {
    uint32_t available = static_cast<uint32_t>(std::min<uint64_t>(
        total_data_length_, std::numeric_limits<uint32_t>::max()));
    if (must_fill_length && length > available) {
        return SharedBufferSpan();
    }
    if (length == 0) {
        return GetSharedReadableSpan();
    }

    for (const auto& state : chunks_) {
        if (!state.span.Valid()) {
            continue;
        }
        uint32_t readable = state.Readable();
        if (readable == 0) {
            continue;
        }
        if (length <= readable) {
            return SharedBufferSpan(state.span.GetChunk(),
                                    state.DataStart() + state.read_offset,
                                    state.DataStart() + state.read_offset + length);
        }
        break;
    }

    return const_cast<MultiBlockBuffer*>(this)->GetSharedBufferSpan(length);
}

std::shared_ptr<IBufferChunk> MultiBlockBuffer::GetChunk() const {
    return nullptr;
}

std::shared_ptr<IBuffer> MultiBlockBuffer::ShallowClone() const {
    auto clone = std::make_shared<MultiBlockBuffer>(pool_);
    clone->chunks_ = chunks_;
    clone->total_data_length_ = total_data_length_;
    return clone;
}

bool MultiBlockBuffer::Empty() const {
    return total_data_length_ == 0;
}

SharedBufferSpan MultiBlockBuffer::GetSharedBufferSpan(uint32_t except_len) {
    if (except_len == 0 || chunks_.empty()) {
        return SharedBufferSpan();
    }

    if (chunks_.size() == 1) {
        const auto& state = chunks_.front();
        uint32_t readable = state.Readable();
        if (readable == 0 || !state.span.Valid()) {
            return SharedBufferSpan();
        }
        if (readable <= except_len) {
            return SharedBufferSpan(state.span.GetChunk(),
                                    state.DataStart() + state.read_offset,
                                    state.DataStart() + state.write_limit);
        }
        return DuplicateSpan(state, except_len);
    }

    // Multiple chunks
    const auto& first = chunks_.front();
    if (!first.span.Valid()) {
        return SharedBufferSpan();
    }

    uint32_t readable_first = first.Readable();
    if (readable_first == 0) {
        return SharedBufferSpan();
    }

    if (readable_first >= except_len) {
        return DuplicateSpan(first, except_len);
    }

    if (!pool_) {
        LOG_ERROR("no memory pool available for duplication");
        return SharedBufferSpan();
    }

    auto new_chunk = std::make_shared<BufferChunk>(pool_);
    if (!new_chunk || !new_chunk->Valid()) {
        LOG_ERROR("failed to allocate new chunk for GetSharedBufferSpan");
        return SharedBufferSpan();
    }

    uint32_t capacity = new_chunk->GetLength();
    if (capacity == 0) {
        return SharedBufferSpan();
    }

    uint8_t* dest = new_chunk->GetData();
    uint32_t target = std::min(except_len, capacity);
    uint32_t remaining = target;
    for (const auto& state : chunks_) {
        if (!state.span.Valid()) {
            continue;
        }
        uint32_t readable = state.Readable();
        if (readable == 0) {
            continue;
        }
        uint32_t copy_len = std::min(readable, remaining);
        std::memcpy(dest, state.DataStart() + state.read_offset, copy_len);
        dest += copy_len;
        remaining -= copy_len;
        if (remaining == 0) {
            break;
        }
    }

    uint32_t produced = target - remaining;
    return SharedBufferSpan(new_chunk, new_chunk->GetData(), new_chunk->GetData() + produced);
}

bool MultiBlockBuffer::EnsureWritableChunk(uint32_t min_free) {
    if (min_free == 0) {
        min_free = 1;
    }

    if (!chunks_.empty()) {
        auto& back = chunks_.back();
        if (back.span.Valid()) {
            auto chunk = back.span.GetChunk();
            if (chunk) {
                uint32_t capacity = chunk->GetLength();
                if (capacity > back.write_limit) {
                    uint32_t free_len = capacity - back.write_limit;
                    if (free_len >= min_free) {
                        return true;
                    }
                }
            }
        }
    }

    if (!pool_) {
        LOG_ERROR("memory pool is nullptr");
        return false;
    }

    auto chunk = std::make_shared<BufferChunk>(pool_);
    if (!chunk || !chunk->Valid()) {
        LOG_ERROR("failed to allocate buffer chunk");
        return false;
    }

    if (chunk->GetLength() < min_free) {
        LOG_ERROR("buffer chunk too small: have %u need %u", chunk->GetLength(), min_free);
        return false;
    }

    ChunkState state;
    state.span = SharedBufferSpan(chunk, chunk->GetData(), chunk->GetLength());
    state.read_offset = 0;
    state.write_limit = 0;
    chunks_.push_back(std::move(state));
    return true;
}

SharedBufferSpan MultiBlockBuffer::DuplicateSpan(const ChunkState& state, uint32_t copy_len) {
    if (!state.span.Valid()) {
        return SharedBufferSpan();
    }

    auto pool = pool_;
    if (!pool) {
        auto chunk = state.span.GetChunk();
        if (chunk) {
            pool = chunk->GetPool();
        }
    }
    if (!pool) {
        LOG_ERROR("memory pool is nullptr");
        return SharedBufferSpan();
    }

    auto new_chunk = std::make_shared<BufferChunk>(pool);
    if (!new_chunk || !new_chunk->Valid()) {
        LOG_ERROR("failed to allocate new chunk for duplication");
        return SharedBufferSpan();
    }

    uint32_t capacity = new_chunk->GetLength();
    if (capacity == 0) {
        return SharedBufferSpan();
    }

    uint32_t copy = std::min(copy_len, capacity);
    std::memcpy(new_chunk->GetData(), state.DataStart() + state.read_offset, copy);
    return SharedBufferSpan(new_chunk, new_chunk->GetData(), new_chunk->GetData() + copy);
}

}
}
