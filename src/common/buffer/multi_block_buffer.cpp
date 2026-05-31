#include <algorithm>
#include <cstring>

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_chunk.h"
#include "common/buffer/multi_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"
#include "common/log/log.h"
#include <quicx/common/metrics.h>
#include <quicx/common/metrics_std.h>

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

    if (chunks_.empty()) {
        LOG_ERROR("no chunks available");
        return SharedBufferSpan();
    }

    // Locate the first chunk that has any readable bytes (skipping any leading
    // already-drained chunks). All bytes of this span are guaranteed to come
    // from a single chunk — invariant 3 forbids us from silently merging
    // across chunks.
    const ChunkState* first = nullptr;
    for (const auto& state : chunks_) {
        if (!state.chunk_ || !state.chunk_->Valid()) {
            continue;
        }
        if (state.Readable() == 0) {
            continue;
        }
        first = &state;
        break;
    }
    if (first == nullptr) {
        LOG_ERROR("no readable chunk available");
        return SharedBufferSpan();
    }

    uint32_t first_readable = first->Readable();
    if (length > first_readable) {
        // Cross-chunk request. Strict callers (must_fill_length=true) get an
        // invalid span; lenient callers get only the first chunk's slice and
        // are expected to either accept the partial result or use the
        // explicit GetSharedReadableSpans / GetFirstChunkReadable APIs.
        if (must_fill_length) {
            LOG_DEBUG(
                "GetSharedReadableSpan would require crossing chunks "
                "(have %u in first chunk, need %u); refusing under invariant 3",
                first_readable, length);
            return SharedBufferSpan();
        }
        length = first_readable;
    }

    return SharedBufferSpan(first->chunk_, first->read_pos_, first->read_pos_ + length);
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

// Invariant 3 (cross-segment must be explicit): hand back exactly one
// chunk's worth of readable bytes — the leading chunk that still holds
// data — capped at max_length. Always zero-copy: the returned span aliases
// the chunk in place and pins its write floor via the SharedBufferSpan
// constructor (invariant 1).
SharedBufferSpan MultiBlockBuffer::GetFirstChunkReadable(uint32_t max_length) const {
    if (max_length == 0) {
        return SharedBufferSpan();
    }
    for (const auto& state : chunks_) {
        if (!state.chunk_ || !state.chunk_->Valid()) {
            continue;
        }
        uint32_t readable = state.Readable();
        if (readable == 0) {
            continue;
        }
        uint32_t take = std::min(readable, max_length);
        return SharedBufferSpan(state.chunk_, state.read_pos_, state.read_pos_ + take);
    }
    return SharedBufferSpan();
}

// Coalesce-on-demand variant. Fast path: if the first non-empty chunk has
// >= max_length readable bytes (or it is the *only* readable chunk), this
// degenerates to GetFirstChunkReadable's zero-copy span — no allocation,
// no memcpy. Slow path: only when the first chunk is partially drained
// AND there is more data behind it, we allocate one StandaloneBufferChunk
// of size `take` and memcpy bytes into it. The resulting span owns the
// standalone chunk via shared_ptr so the buffer's MoveReadPt is unaffected.
SharedBufferSpan MultiBlockBuffer::GetCoalescedReadable(uint32_t max_length) const {
    if (max_length == 0 || chunks_.empty() || total_data_length_ == 0) {
        return SharedBufferSpan();
    }

    // Locate the first chunk with readable bytes.
    auto it = chunks_.begin();
    for (; it != chunks_.end(); ++it) {
        if (it->chunk_ && it->chunk_->Valid() && it->Readable() > 0) {
            break;
        }
    }
    if (it == chunks_.end()) {
        return SharedBufferSpan();
    }

    uint32_t first_readable = it->Readable();

    // Fast path A: first chunk satisfies the request → zero-copy span.
    if (first_readable >= max_length) {
        return SharedBufferSpan(it->chunk_, it->read_pos_, it->read_pos_ + max_length);
    }

    // Fast path B: first chunk is the only chunk with data → there is
    // nothing to coalesce; return whatever the first chunk has.
    uint64_t total_readable = total_data_length_;  // already tracked
    if (first_readable >= total_readable) {
        return SharedBufferSpan(it->chunk_, it->read_pos_, it->read_pos_ + first_readable);
    }

    // Slow path: stitch a contiguous prefix of size `take` across chunks.
    uint32_t take = static_cast<uint32_t>(std::min<uint64_t>(max_length, total_readable));
    auto standalone = std::make_shared<StandaloneBufferChunk>(take);
    if (!standalone || !standalone->Valid()) {
        // Allocation failed → degrade gracefully to the first-chunk-only span.
        LOG_ERROR("GetCoalescedReadable: failed to allocate standalone chunk size=%u", take);
        return SharedBufferSpan(it->chunk_, it->read_pos_, it->read_pos_ + first_readable);
    }

    uint8_t* dst = standalone->GetData();
    uint32_t copied = 0;
    for (auto cur = it; cur != chunks_.end() && copied < take; ++cur) {
        if (!cur->chunk_ || !cur->chunk_->Valid()) {
            continue;
        }
        uint32_t r = cur->Readable();
        if (r == 0) {
            continue;
        }
        uint32_t to_copy = std::min(r, take - copied);
        std::memcpy(dst + copied, cur->read_pos_, to_copy);
        copied += to_copy;
    }
    // copied is guaranteed to equal `take` because total_readable >= take.
    return SharedBufferSpan(standalone, dst, dst + copied);
}

// Invariant 3: the explicit segmented readout. We walk chunks in read order
// and emit one SharedBufferSpan per chunk we touch, stopping once we have
// covered min(length, total_data_length_) bytes. No memcpy, no allocation:
// the caller is fully informed that the prefix may live in multiple chunks
// and is responsible for consuming them as a list.
std::vector<SharedBufferSpan> MultiBlockBuffer::GetSharedReadableSpans(uint32_t length) const {
    std::vector<SharedBufferSpan> out;
    if (chunks_.empty() || total_data_length_ == 0) {
        return out;
    }
    // total_data_length_ is uint64_t but per-call cap fits in uint32_t (in
    // practice it's bounded by chunk-pool capacity and per-frame send size).
    uint32_t available = (total_data_length_ > UINT32_MAX)
                             ? UINT32_MAX
                             : static_cast<uint32_t>(total_data_length_);
    uint32_t remaining = (length == 0) ? available : std::min(length, available);
    out.reserve(chunks_.size());
    for (const auto& state : chunks_) {
        if (remaining == 0) {
            break;
        }
        if (!state.chunk_ || !state.chunk_->Valid()) {
            continue;
        }
        uint32_t readable = state.Readable();
        if (readable == 0) {
            continue;
        }
        uint32_t take = std::min(readable, remaining);
        out.emplace_back(state.chunk_, state.read_pos_, state.read_pos_ + take);
        remaining -= take;
    }
    return out;
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

    // Advance source buffer's read pointer by the amount written
    if (written > 0) {
        buffer->MoveReadPt(written);
    }

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

    // PERF DIAG (datagram fill): distribution of how big each span actually
    // is. The first_chunk_h bimodal pattern (big-or-tiny) on the read side
    // can only come from this write side: either the upper layer is feeding
    // tiny spans interspersed with large ones, or this function is creating
    // tiny ChunkStates by itself. The histogram disambiguates.
    Metrics::HistogramObserve(MetricsStd::DiagSpanWriteHist, data_len);

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
