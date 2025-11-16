#ifndef COMMON_BUFFER_SINGLE_BLOCK_BUFFER
#define COMMON_BUFFER_SINGLE_BLOCK_BUFFER

#include <cstdint>
#include <memory>
#include <functional>

#include "common/buffer/if_buffer.h"
#include "common/buffer/buffer_span.h"
#include "common/buffer/if_buffer_chunk.h"
#include "common/buffer/buffer_read_view.h"
#include "common/buffer/shared_buffer_span.h"

namespace quicx {
namespace common {

// SingleBlockBuffer manages read/write operations on a single BufferChunk and
// fully implements the IBuffer contract. The caller is responsible for
// supplying the backing chunk (which may come from either BlockMemoryPool via
// BufferChunk or StandaloneBufferChunk). The class is not thread-safe.
class SingleBlockBuffer:
    public IBuffer {
public:
    // Construct an empty buffer without backing storage.
    SingleBlockBuffer();
    // Take ownership of an existing chunk.
    explicit SingleBlockBuffer(std::shared_ptr<IBufferChunk> chunk);
    ~SingleBlockBuffer() = default;

    SingleBlockBuffer(const SingleBlockBuffer&) = delete;
    SingleBlockBuffer& operator=(const SingleBlockBuffer&) = delete;

    SingleBlockBuffer(SingleBlockBuffer&& other) noexcept;
    SingleBlockBuffer& operator=(SingleBlockBuffer&& other) noexcept;

    // Returns true when the buffer has a valid backing chunk.
    bool Valid() const { return chunk_ && chunk_->Valid(); }

    // Copy readable data without advancing the read pointer.
    uint32_t ReadNotMovePt(uint8_t* data, uint32_t len) override;
    // Advance (or rewind) the read pointer safely.
    uint32_t MoveReadPt(int32_t len) override;
    // Copy readable data and advance the read pointer.
    uint32_t Read(uint8_t* data, uint32_t len) override;
    void VisitData(const std::function<void(uint8_t*, uint32_t)>& visitor) override;
    void VisitDataSpans(const std::function<void(SharedBufferSpan&)>& visitor) override;
    // Number of bytes currently readable.
    uint32_t GetDataLength() override;
    // Pointer to the readable region.
    uint8_t* GetData() const;
    // Return a read-only view over the readable data.
    BufferReadView GetReadView() const override;
    // Return a non-owning span describing the readable data.
    BufferSpan GetWritableSpan() const;
    BufferSpan GetReadableSpan() const override;
    // Return a shared span that keeps the chunk alive.
    SharedBufferSpan GetSharedBufferSpan() const;
    SharedBufferSpan GetSharedReadableSpan() const override;
    SharedBufferSpan GetSharedReadableSpan(uint32_t length) const override;
    SharedBufferSpan GetSharedReadableSpan(uint32_t length, bool must_fill_length) const override;
    // Reset read/write pointers without releasing the chunk.
    void Clear() override;
    // Return the data as a string.
    std::string GetDataAsString() override;

    // Write data into the buffer and move the write pointer.
    uint32_t Write(const uint8_t* data, uint32_t len) override;
    uint32_t Write(std::shared_ptr<IBuffer> buffer) override;
    uint32_t Write(const SharedBufferSpan& span) override;
    uint32_t Write(const SharedBufferSpan& span, uint32_t data_len) override;
    // Remaining writable capacity.
    uint32_t GetFreeLength() override;
    // Advance (or rewind) the write pointer safely.
    uint32_t MoveWritePt(int32_t len) override;
    BufferSpan GetWritableSpan() override;
    BufferSpan GetWritableSpan(uint32_t expected_length) override;

    // Replace the current chunk with a new one.
    void Reset(std::shared_ptr<IBufferChunk> chunk);
    // return the chunk of the buffer
    std::shared_ptr<IBufferChunk> GetChunk() const override;
    std::shared_ptr<IBuffer> ShallowClone() const override;
private:
    // Core implementation for read operations.
    uint32_t InnerRead(uint8_t* data, uint32_t len, bool move_pt);
    // Core implementation for write operations.
    uint32_t InnerWrite(const uint8_t* data, uint32_t len);
    // Initialize internal pointers after a new chunk is attached.
    void InitializePointers();

    std::shared_ptr<IBufferChunk> chunk_;
    uint8_t* read_pos_ = nullptr;
    uint8_t* write_pos_ = nullptr;
    uint8_t* buffer_start_ = nullptr;
    uint8_t* buffer_end_ = nullptr;
};

}
}

#endif

