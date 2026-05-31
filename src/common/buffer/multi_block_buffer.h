#ifndef COMMON_BUFFER_MULTI_BLOCK_BUFFER
#define COMMON_BUFFER_MULTI_BLOCK_BUFFER

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

#include "common/alloter/pool_block.h"
#include "common/buffer/if_buffer.h"
#include "common/buffer/shared_buffer_span.h"

namespace quicx {
namespace common {

// MultiBlockBuffer manages a queue of IBufferChunk objects to provide a
// growable read/write buffer. It mirrors the data access APIs of the legacy
// Buffer implementation but intentionally avoids exposing raw chunk_ ownership
// or span/view helpers. Because it stores IBufferChunk instances it can
// safely extend across asynchronous pipelines without worrying about dangling
// memory – each span keeps the underlying BufferChunk alive.
class MultiBlockBuffer: public IBuffer, public std::enable_shared_from_this<MultiBlockBuffer> {
public:
    MultiBlockBuffer() = default;
    explicit MultiBlockBuffer(std::shared_ptr<BlockMemoryPool> pool, bool pre_allocate = true);

    // Reset the buffer to an empty state. Equivalent to Clear().
    void Reset();
    void Clear() override;

    void SetPool(const std::shared_ptr<BlockMemoryPool>& pool) { pool_ = pool; }

    // Read helpers replicate the behaviour of the legacy Buffer: ReadNotMovePt
    // copies without advancing the read pointer, Read consumes the data, and
    // MoveReadPt moves the pointer without copying.
    uint32_t ReadNotMovePt(uint8_t* data, uint32_t len) override;
    uint32_t MoveReadPt(uint32_t len) override;
    uint32_t Read(uint8_t* data, uint32_t len) override;
    void VisitData(const std::function<bool(uint8_t*, uint32_t)>& visitor) override;
    uint32_t GetDataLength() override;

    // *************************** inner interfaces ***************************
    std::shared_ptr<IBuffer> CloneReadable(uint32_t length, bool move_write_pt = true) override;
    BufferSpan GetReadableSpan() const override;
    SharedBufferSpan GetSharedReadableSpan(uint32_t length = 0, bool must_fill_length = false) const override;
    std::string GetDataAsString() override;
    void VisitDataSpans(const std::function<bool(SharedBufferSpan&)>& visitor) override;

    // Zero-copy invariant 3 (cross-segment must be explicit) ------------------
    //
    // GetFirstChunkReadable: hand back a SharedBufferSpan covering the
    // *first* chunk's currently readable bytes, capped at `max_length`. The
    // span is always confined to a single chunk and is therefore guaranteed
    // zero-copy. Returns an invalid span if the buffer has no readable data.
    //
    // Use this when emitting a single contiguous frame (e.g. one STREAM frame
    // per call) and the caller is happy to let later iterations drain
    // subsequent chunks.
    SharedBufferSpan GetFirstChunkReadable(uint32_t max_length) const;

    // GetCoalescedReadable: like GetFirstChunkReadable, but if the first
    // chunk's readable region is shorter than max_length AND there is
    // additional readable data in subsequent chunks, allocate a single
    // StandaloneBufferChunk and memcpy the prefix of the readable bytes
    // (up to max_length) into it, returning a span that covers exactly that
    // contiguous region. This trades one bounded memcpy (≤max_length bytes)
    // for the ability to fill a single STREAM frame to its packet-level cap
    // even when the first chunk is partially drained.
    //
    // Why this exists:
    //   MultiBlockBuffer fragments steady-state when the consumer reads a
    //   non-multiple of the underlying chunk size each iteration: a 1500B
    //   chunk yields a 1234B span, leaving 266B in the first chunk. New
    //   producer writes go to a fresh chunk (because the first one has
    //   Writable()==0). The next read therefore sees only 266B in the
    //   first chunk. Asking for "up to max_length contiguous bytes"
    //   collapses this fragmentation by stitching the 266B tail with the
    //   head of the next chunk.
    //
    // Cost vs zero-copy invariant 3:
    //   StreamFrame::Encode already memcpy's the payload into the packet
    //   buffer (BufferEncodeWrapper::EncodeBytes), so adding a small
    //   coalesce memcpy here only changes constants, not big-O. It is
    //   strictly cheaper than the alternative (sending a tiny 266B STREAM
    //   frame plus a full-size one in a follow-up packet, which doubles
    //   the per-byte UDP send overhead).
    //
    // Returns an invalid span if the buffer has no readable data.
    SharedBufferSpan GetCoalescedReadable(uint32_t max_length) const;

    // GetSharedReadableSpans: hand back one SharedBufferSpan *per chunk* the
    // requested prefix touches, in read order, totalling
    //   min(length, GetDataLength())
    // bytes. Each span is zero-copy (no chunk allocation, no memcpy). The
    // caller is responsible for stitching them together, e.g. by writing them
    // sequentially into a fresh buffer or feeding them to scatter-gather IO.
    //
    // length == 0 means "all currently readable bytes".
    std::vector<SharedBufferSpan> GetSharedReadableSpans(uint32_t length = 0) const;

    // Write helpers enqueue externally managed spans. The overload with
    // data_len allows callers to cap the exposed readable portion (useful when
    // only a prefix of the span contains meaningful data).
    uint32_t Write(const uint8_t* data, uint32_t len) override;
    uint32_t Write(const SharedBufferSpan& span) override;
    uint32_t Write(std::shared_ptr<IBuffer> buffer) override;
    uint32_t Write(const SharedBufferSpan& span, uint32_t data_len) override;
    uint32_t GetFreeLength() override;
    uint32_t MoveWritePt(uint32_t len) override;
    BufferSpan GetWritableSpan() override;
    BufferSpan GetWritableSpan(uint32_t expected_length) override;
    std::shared_ptr<IBufferChunk> GetChunk() const override;
    uint32_t GetChunkCount() const override;

    // Additional helper functions
    uint32_t Write(std::shared_ptr<IBufferRead> buffer);
    uint32_t Write(std::shared_ptr<IBufferRead> buffer, uint32_t data_len);
    bool Empty() const;

private:
    void CheckWritable();

    struct ChunkState {
        std::shared_ptr<IBufferChunk> chunk_;
        uint8_t* read_pos_ = nullptr;
        uint8_t* write_pos_ = nullptr;

        uint32_t Writable() const { return chunk_->GetLength() - (write_pos_ - chunk_->GetData()); }
        uint32_t Readable() const { return write_pos_ - read_pos_; }
        uint8_t* DataStart() const { return read_pos_; }
    };

    std::shared_ptr<IBufferChunk> EnsureWritableChunk();

    std::shared_ptr<BlockMemoryPool> pool_;
    std::deque<ChunkState> chunks_;  // ordered list of readable spans
    uint64_t total_data_length_ = 0;
};

}  // namespace common
}  // namespace quicx

#endif
