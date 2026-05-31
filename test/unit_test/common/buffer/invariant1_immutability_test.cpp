// =============================================================================
// Buffer Invariant 1 - Immutability of issued slices (write-floor / freeze)
// =============================================================================
//
// Contract under test:
//
//   Once a SharedBufferSpan has been issued from a buffer, the bytes covered
//   by that span MUST remain unchanged for the lifetime of the span, even if
//   the originating buffer is reset, cleared, or written to again.
//
// Mechanism: every chunk carries a write-floor (lowest writable position).
// SharedBufferSpan construction freezes the chunk up to span.end(); any
// subsequent attempt to roll the write pointer back below the floor
// (Clear/InitializePointers/InnerWrite) is silently clamped to the floor.
// Reads remain unaffected; only writes are constrained.
//
// All tests live in the dedicated suite `BufferInvariant1_Immutability` so
// they can be run/skipped independently from the legacy regression tests in
// `SingleBlockBufferTest.*`.
//
// These tests are intentionally written BEFORE the production change. They
// MUST be RED at first (Clear() currently rewinds unconditionally) and turn
// GREEN once the freeze hook lands in BufferChunk + SharedBufferSpan +
// SingleBlockBuffer.
// =============================================================================

#include <algorithm>
#include <array>
#include <cstdint>
#include <numeric>
#include <vector>

#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_chunk.h"
#include "common/buffer/shared_buffer_span.h"
#include "common/buffer/single_block_buffer.h"

namespace quicx {
namespace common {
namespace {

// Helper: build a pool-backed chunk with a single block of the requested size.
std::shared_ptr<BufferChunk> MakeChunk(uint32_t size) {
    auto pool = MakeBlockMemoryPoolPtr(size, /*add_num=*/1u);
    auto chunk = std::make_shared<BufferChunk>(pool);
    EXPECT_TRUE(chunk && chunk->Valid());
    return chunk;
}

// Helper: monotonically-increasing payload, easy to spot any byte that mutates
// out from under us.
std::vector<uint8_t> MakePattern(uint32_t len, uint8_t seed = 1) {
    std::vector<uint8_t> v(len);
    for (uint32_t i = 0; i < len; ++i) v[i] = static_cast<uint8_t>(seed + i);
    return v;
}

// =============================================================================
// B1-01  Clear() must not rewrite bytes that an outstanding span references.
// =============================================================================
TEST(BufferInvariant1_Immutability, ClearDoesNotMutateIssuedSpan) {
    auto chunk = MakeChunk(4096);
    auto buf = std::make_shared<SingleBlockBuffer>(chunk);

    auto payload = MakePattern(500);
    ASSERT_EQ(500u, buf->Write(payload.data(), payload.size()));

    // Take an outstanding zero-copy view over the buffer's bytes.
    auto span = buf->GetSharedReadableSpan(500);
    ASSERT_TRUE(span.Valid());
    ASSERT_EQ(500u, span.GetLength());

    // Snapshot the bytes the span sees right now.
    std::vector<uint8_t> before(span.GetStart(), span.GetEnd());

    // Try to "reset and reuse" the buffer while the span is still alive. If
    // Clear() naively rewinds write_pos_ to buffer_start_, a subsequent Write
    // would silently overwrite the bytes the span is still holding.
    buf->Clear();
    auto overwrite = MakePattern(500, /*seed=*/0xA0);
    buf->Write(overwrite.data(), overwrite.size());

    // The span's view must remain identical to the original payload.
    std::vector<uint8_t> after(span.GetStart(), span.GetEnd());
    ASSERT_EQ(before, after);
    for (uint32_t i = 0; i < 500; ++i) {
        EXPECT_EQ(static_cast<uint8_t>(1 + i), span.GetStart()[i]) << "i=" << i;
    }
}

// =============================================================================
// B1-02  After span goes out of scope, the floor is released and the buffer
//        becomes fully reusable again. Floor must not be a permanent leak.
// =============================================================================
TEST(BufferInvariant1_Immutability, FloorReleasedAfterSpanDestroyed) {
    auto chunk = MakeChunk(4096);
    auto buf = std::make_shared<SingleBlockBuffer>(chunk);

    auto payload = MakePattern(500);
    ASSERT_EQ(500u, buf->Write(payload.data(), payload.size()));

    {
        auto span = buf->GetSharedReadableSpan(500);
        ASSERT_TRUE(span.Valid());
        // While span is alive, Clear is constrained.
        buf->Clear();
        // Free space must NOT have grown back to full capacity, because the
        // first 500 bytes are still pinned.
        EXPECT_LE(buf->GetFreeLength(), 4096u - 500u);
    }
    // span destroyed -> chunk still alive (buf holds it), but the floor that
    // span installed must now be lifted.

    buf->Clear();
    EXPECT_EQ(4096u, buf->GetFreeLength())
        << "after the last span is gone Clear() must fully reclaim the chunk";

    // And the freshly cleared buffer must be writable from the very first byte.
    auto fresh = MakePattern(700, /*seed=*/0x40);
    EXPECT_EQ(700u, buf->Write(fresh.data(), fresh.size()));
}

// =============================================================================
// B1-03  Multiple overlapping spans -> floor honours the highest end-pointer.
// =============================================================================
TEST(BufferInvariant1_Immutability, MultipleSpansFloorIsHighWatermark) {
    auto chunk = MakeChunk(4096);
    auto buf = std::make_shared<SingleBlockBuffer>(chunk);

    auto payload = MakePattern(1000);
    ASSERT_EQ(1000u, buf->Write(payload.data(), payload.size()));

    auto span_short = buf->GetSharedReadableSpan(200);     // covers [0, 200)
    ASSERT_TRUE(span_short.Valid());
    // Drop the first 200 bytes from the buffer's read pointer so the next span
    // starts at offset 200, but the chunk floor must still be at 200 because
    // span_short still references the prefix.
    EXPECT_EQ(200u, buf->MoveReadPt(200));

    auto span_long = buf->GetSharedReadableSpan(800);      // covers [200, 1000)
    ASSERT_TRUE(span_long.Valid());
    EXPECT_EQ(800u, span_long.GetLength());

    // While both spans are alive, Clear() must not rewind below the higher
    // watermark (1000). Free space stays bounded by capacity - 1000.
    buf->Clear();
    EXPECT_LE(buf->GetFreeLength(), 4096u - 1000u);

    // Snapshot both spans' bytes; they must equal what we originally wrote.
    for (uint32_t i = 0; i < 200; ++i) {
        EXPECT_EQ(static_cast<uint8_t>(1 + i), span_short.GetStart()[i]);
    }
    for (uint32_t i = 0; i < 800; ++i) {
        EXPECT_EQ(static_cast<uint8_t>(1 + 200 + i), span_long.GetStart()[i]);
    }
}

// =============================================================================
// B1-04  InnerWrite() (i.e. Write() after Clear) must respect the floor and
//        never start writing inside an outstanding span.
// =============================================================================
TEST(BufferInvariant1_Immutability, WriteAfterClearStartsAtOrAboveFloor) {
    auto chunk = MakeChunk(4096);
    auto buf = std::make_shared<SingleBlockBuffer>(chunk);

    auto payload = MakePattern(300);
    ASSERT_EQ(300u, buf->Write(payload.data(), payload.size()));
    auto span = buf->GetSharedReadableSpan(300);
    ASSERT_TRUE(span.Valid());

    // The user thinks Clear "resets" the buffer.
    buf->Clear();

    // Write a clearly different pattern; the bytes the span still references
    // must not change.
    auto overwrite = MakePattern(300, /*seed=*/0xE0);
    buf->Write(overwrite.data(), overwrite.size());

    for (uint32_t i = 0; i < 300; ++i) {
        EXPECT_EQ(static_cast<uint8_t>(1 + i), span.GetStart()[i])
            << "byte " << i << " was clobbered through the floor";
    }
}

// =============================================================================
// B1-05  GetWriteFloor() reflects the highest end-pointer of any live span.
// =============================================================================
TEST(BufferInvariant1_Immutability, GetWriteFloorReportsHighestEnd) {
    auto chunk = MakeChunk(4096);
    auto buf = std::make_shared<SingleBlockBuffer>(chunk);

    auto payload = MakePattern(700);
    ASSERT_EQ(700u, buf->Write(payload.data(), payload.size()));

    // No spans yet -> floor is the chunk start.
    EXPECT_EQ(chunk->GetData(), chunk->GetWriteFloor());

    {
        auto span = buf->GetSharedReadableSpan(700);
        ASSERT_TRUE(span.Valid());
        EXPECT_EQ(chunk->GetData() + 700, chunk->GetWriteFloor());
    }
    // Span gone -> floor reverts to chunk start.
    EXPECT_EQ(chunk->GetData(), chunk->GetWriteFloor());
}

// =============================================================================
// B1-06  Reset() to a different chunk must not be observable by callers
//        holding a span over the OLD chunk: the old chunk lives until the
//        span dies and its bytes stay frozen.
// =============================================================================
TEST(BufferInvariant1_Immutability, ResetDoesNotInvalidateOutstandingSpan) {
    auto chunk_a = MakeChunk(4096);
    auto buf = std::make_shared<SingleBlockBuffer>(chunk_a);

    auto payload = MakePattern(400);
    ASSERT_EQ(400u, buf->Write(payload.data(), payload.size()));
    auto span = buf->GetSharedReadableSpan(400);
    ASSERT_TRUE(span.Valid());

    // Hand the buffer a brand-new chunk.
    auto chunk_b = MakeChunk(4096);
    buf->Reset(chunk_b);

    // Writing into the new chunk must not affect the bytes the span references
    // in the old chunk.
    auto overwrite = MakePattern(400, /*seed=*/0xC0);
    buf->Write(overwrite.data(), overwrite.size());

    for (uint32_t i = 0; i < 400; ++i) {
        EXPECT_EQ(static_cast<uint8_t>(1 + i), span.GetStart()[i]);
    }
    // And the span still owns its old chunk via shared_ptr -> no UAF.
    EXPECT_TRUE(span.Valid());
    EXPECT_EQ(chunk_a, span.GetChunk());
}

}  // namespace
}  // namespace common
}  // namespace quicx
