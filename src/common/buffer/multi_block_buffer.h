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
// Buffer implementation but intentionally avoids exposing raw chunk_ ownership
// or span/view helpers. Because it stores SharedBufferSpan instances it can
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
    BufferReadView GetReadView() const override;
    BufferSpan GetReadableSpan() const override;
    SharedBufferSpan GetSharedReadableSpan(uint32_t length = 0, bool must_fill_length = false) const override;
    std::string GetDataAsString() override;
    void VisitDataSpans(const std::function<bool(SharedBufferSpan&)>& visitor) override;

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

    // Wait data trigger support
    uint32_t wait_data_required_length_{0};
    std::function<void(std::shared_ptr<IBuffer>)> wait_data_callback_;
};

}  // namespace common
}  // namespace quicx

#endif
