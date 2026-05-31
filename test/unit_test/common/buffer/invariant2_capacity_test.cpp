// Zero-copy invariant 2: Physical capacity is decoupled from logical/visible
// length.
//
// In the legacy design, IBufferChunk exposed SetLimitSize() and GetLength()
// returned min(physical, limit). That entangled two concerns:
//   * how many bytes the underlying memory block actually owns (a fact about
//     the chunk), and
//   * how many bytes a particular IBuffer view is allowed to use (a policy
//     of the buffer/encoder, e.g. "fit one MTU-sized QUIC packet").
//
// The new contract:
//   * IBufferChunk::GetLength() always reports the physical block size and
//     is immutable for the lifetime of the chunk.
//   * SingleBlockBuffer::SetCapacityLimit(N) installs a per-buffer cap on the
//     writable region. Writes never spill past it; the chunk itself is
//     untouched.
//   * Setting a limit larger than the chunk simply clamps to the physical
//     size; setting a smaller limit shrinks GetFreeLength().
//
// These tests pin that contract before the implementation switches over,
// hence they are expected to be RED until B2 is implemented.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_chunk.h"
#include "common/buffer/single_block_buffer.h"

namespace quicx {
namespace common {
namespace {

constexpr uint32_t kBlockSize = 4096;

std::shared_ptr<BufferChunk> MakeChunk() {
    auto pool = MakeBlockMemoryPoolPtr(kBlockSize, 1);
    return std::make_shared<BufferChunk>(pool);
}

// ----- B2-01 ----------------------------------------------------------------
// The chunk's physical length is a property of its storage and cannot be
// shrunk by any consumer-facing operation. It must always equal kBlockSize.
TEST(BufferInvariant2_Capacity, ChunkLengthIsPhysical) {
    auto chunk = MakeChunk();
    ASSERT_TRUE(chunk->Valid());
    EXPECT_EQ(kBlockSize, chunk->GetLength());

    // Even after a buffer wraps the chunk and applies a much smaller capacity
    // limit, the chunk itself reports the same physical size.
    auto buf = std::make_shared<SingleBlockBuffer>(chunk);
    buf->SetCapacityLimit(64);
    EXPECT_EQ(kBlockSize, chunk->GetLength());
}

// ----- B2-02 ----------------------------------------------------------------
// SetCapacityLimit(N) on a fresh buffer must cap the writable region to N
// bytes, regardless of the chunk's larger physical size.
TEST(BufferInvariant2_Capacity, BufferCapacityLimitShrinksFreeLength) {
    auto chunk = MakeChunk();
    auto buf = std::make_shared<SingleBlockBuffer>(chunk);
    EXPECT_EQ(kBlockSize, buf->GetFreeLength());

    buf->SetCapacityLimit(128);
    EXPECT_EQ(128u, buf->GetFreeLength());
}

// ----- B2-03 ----------------------------------------------------------------
// SetCapacityLimit must not be observable through the chunk. This is the
// invariant that the legacy SetLimitSize broke (chunk->GetLength() shrank
// underneath any other view of the chunk).
TEST(BufferInvariant2_Capacity, BufferCapacityLimitDoesNotMutateChunkLength) {
    auto chunk = MakeChunk();
    auto buf = std::make_shared<SingleBlockBuffer>(chunk);

    buf->SetCapacityLimit(32);
    EXPECT_EQ(kBlockSize, chunk->GetLength());

    // A second view of the chunk would still see the full physical size if it
    // wrapped the chunk independently. We don't construct one here (a chunk
    // is normally exclusively owned by a buffer), but the invariant
    // chunk->GetLength() == physical is the load-bearing claim.
}

// ----- B2-04 ----------------------------------------------------------------
// Writes never spill past the capacity limit, even when the source buffer
// exceeds it.
TEST(BufferInvariant2_Capacity, WriteRespectsCapacityLimit) {
    auto chunk = MakeChunk();
    auto buf = std::make_shared<SingleBlockBuffer>(chunk);
    buf->SetCapacityLimit(100);

    std::array<uint8_t, 200> payload;
    payload.fill(0xAB);
    uint32_t written = buf->Write(payload.data(), payload.size());
    EXPECT_EQ(100u, written);
    EXPECT_EQ(100u, buf->GetDataLength());
    EXPECT_EQ(0u, buf->GetFreeLength());
}

// ----- B2-05 ----------------------------------------------------------------
// A capacity limit larger than the chunk's physical size is silently clamped
// to the physical size. This keeps the buffer safe from API misuse and
// matches the old "SetLimitSize(100) on a 64-byte chunk → 64" semantics
// without leaking the limit concept down into the chunk.
TEST(BufferInvariant2_Capacity, CapacityLimitIsClampedToPhysical) {
    auto chunk = MakeChunk();
    auto buf = std::make_shared<SingleBlockBuffer>(chunk);

    buf->SetCapacityLimit(kBlockSize * 4);
    EXPECT_EQ(kBlockSize, buf->GetFreeLength());
    EXPECT_EQ(kBlockSize, chunk->GetLength());
}

// ----- B2-06 ----------------------------------------------------------------
// The limit survives across multiple writes; it is a property of the buffer
// view, not consumed on the first write.
TEST(BufferInvariant2_Capacity, LimitSurvivesAcrossWrites) {
    auto chunk = MakeChunk();
    auto buf = std::make_shared<SingleBlockBuffer>(chunk);
    buf->SetCapacityLimit(50);

    std::array<uint8_t, 30> payload;
    payload.fill(0x11);
    EXPECT_EQ(30u, buf->Write(payload.data(), payload.size()));
    EXPECT_EQ(20u, buf->GetFreeLength());

    std::array<uint8_t, 30> more;
    more.fill(0x22);
    // Only 20 of these 30 bytes can land; the remaining 10 must be rejected
    // because the capacity limit (not the chunk!) is exhausted.
    EXPECT_EQ(20u, buf->Write(more.data(), more.size()));
    EXPECT_EQ(50u, buf->GetDataLength());
    EXPECT_EQ(0u, buf->GetFreeLength());
}

}  // namespace
}  // namespace common
}  // namespace quicx
