#ifndef COMMON_BUFFERNEW_MULTI_BLOCK_BUFFER
#define COMMON_BUFFERNEW_MULTI_BLOCK_BUFFER

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>

#include "common/buffer/if_buffer.h"
#include "common/alloter/pool_block.h"
#include "common/buffer/shared_buffer_span.h"

namespace quicx {
namespace common {

// MultiBlockBuffer manages a queue of SharedBufferSpan objects to provide a
// growable read/write buffer. It mirrors the data access APIs of the legacy
// Buffer implementation but intentionally avoids exposing raw chunk ownership
// or span/view helpers. Because it stores SharedBufferSpan instances it can
// safely extend across asynchronous pipelines without worrying about dangling
// memory – each span keeps the underlying BufferChunk alive.
class MultiBlockBuffer:
    public IBuffer {
public:
    MultiBlockBuffer() = default;
    explicit MultiBlockBuffer(std::shared_ptr<BlockMemoryPool> pool);

    // Reset the buffer to an empty state. Equivalent to Clear().
    void Reset();
    void Clear() override;

    void SetPool(const std::shared_ptr<BlockMemoryPool>& pool) { pool_ = pool; }

    // Read helpers replicate the behaviour of the legacy Buffer: ReadNotMovePt
    // copies without advancing the read pointer, Read consumes the data, and
    // MoveReadPt moves the pointer without copying.
    uint32_t ReadNotMovePt(uint8_t* data, uint32_t len) override;
    uint32_t Read(uint8_t* data, uint32_t len) override;
    void VisitData(const std::function<void(uint8_t*, uint32_t)>& visitor) override;
    void VisitDataSpans(const std::function<void(SharedBufferSpan&)>& visitor) override;
    // Write helpers enqueue externally managed spans. The overload with
    // data_len allows callers to cap the exposed readable portion (useful when
    // only a prefix of the span contains meaningful data).
    uint32_t Write(const SharedBufferSpan& span) override;
    uint32_t Write(const SharedBufferSpan& span, uint32_t data_len) override;
    // Copy data into the buffer using internally managed chunks.
    uint32_t Write(const uint8_t* data, uint32_t len) override;
    uint32_t Write(std::shared_ptr<IBufferRead> buffer);
    uint32_t Write(std::shared_ptr<IBuffer> buffer) override;
    uint32_t Write(std::shared_ptr<IBufferRead> buffer, uint32_t data_len);

    uint32_t MoveReadPt(int32_t len) override;
    uint32_t MoveWritePt(int32_t len) override;

    uint32_t GetDataLength() override;
    uint32_t GetFreeLength() override;
    BufferReadView GetReadView() const override;
    BufferSpan GetReadableSpan() const override;
    std::string GetDataAsString() override;
    BufferSpan GetWritableSpan() override;
    BufferSpan GetWritableSpan(uint32_t expected_length) override;
    SharedBufferSpan GetSharedReadableSpan() const override;
    SharedBufferSpan GetSharedReadableSpan(uint32_t length) const override;
    SharedBufferSpan GetSharedReadableSpan(uint32_t length, bool must_fill_length) const override;
    std::shared_ptr<IBufferChunk> GetChunk() const override;
    std::shared_ptr<IBuffer> ShallowClone() const override;
    SharedBufferSpan GetSharedBufferSpan(uint32_t except_len);
    bool Empty() const;

private:
    struct ChunkState {
        SharedBufferSpan span;
        uint32_t read_offset = 0;
        uint32_t write_limit = 0;

        uint32_t Readable() const { return write_limit > read_offset ? write_limit - read_offset : 0; }
        uint8_t* DataStart() const { return span.GetStart(); }
    };

    bool EnsureWritableChunk(uint32_t min_free = 1);
    SharedBufferSpan DuplicateSpan(const ChunkState& state, uint32_t copy_len);

    std::shared_ptr<BlockMemoryPool> pool_;
    std::deque<ChunkState> chunks_;   // ordered list of readable spans
    uint64_t total_data_length_ = 0;
};

}
}

#endif

