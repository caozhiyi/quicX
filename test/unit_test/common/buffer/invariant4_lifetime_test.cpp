// =============================================================================
// Buffer Invariant 4 - Span lifetime outlives the originating buffer
// =============================================================================
//
// Contract under test:
//
//   A SharedBufferSpan handed out by an IBuffer MUST remain safely readable
//   and byte-stable for its entire lifetime, regardless of what the
//   originating buffer subsequently does — including:
//
//     (a) Clear() / Reset()             (queued data wiped)
//     (b) Write() of fresh bytes        (new payload appended to the buffer)
//     (c) MoveReadPt() / Read()         (read pointer advances past the span)
//     (d) destruction of the IBuffer    (buffer object goes away entirely)
//     (e) repeated Clear + Write cycles (state churn without releasing chunks)
//
// Mechanism: SharedBufferSpan carries a shared_ptr<IBufferChunk>, so the
// underlying memory cannot be freed while any span still references it.
// Combined with invariant 1 (write floor freezes already-issued bytes), this
// gives us full temporal independence: callers can park spans on async send
// queues and the buffer can keep churning behind their back without ever
// corrupting them.
//
// Invariant 4 is a TEMPORAL extension of invariant 1: invariant 1 guarantees
// "no in-place mutation of issued bytes"; invariant 4 guarantees "no
// dangling memory either".
//
// These tests are written for the post-B1/B2/B3 codebase; they are expected
// to pass already on healthy implementations and serve as a regression net.
// If they fail, something has regressed in chunk lifetime or write-floor
// enforcement.
// =============================================================================

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_chunk.h"
#include "common/buffer/multi_block_buffer.h"
#include "common/buffer/shared_buffer_span.h"
#include "common/buffer/single_block_buffer.h"

namespace quicx {
namespace common {
namespace {

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

std::vector<uint8_t> Pattern(uint32_t n, uint8_t base = 0x10) {
    std::vector<uint8_t> v;
    v.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        v.push_back(static_cast<uint8_t>(base + i));
    }
    return v;
}

std::shared_ptr<SingleBlockBuffer> MakeSingleBuffer(uint32_t chunk_size) {
    auto pool = MakeBlockMemoryPoolPtr(chunk_size, 4u);
    auto chunk = std::make_shared<BufferChunk>(pool);
    return std::make_shared<SingleBlockBuffer>(chunk);
}

std::shared_ptr<MultiBlockBuffer> MakeMultiBuffer(uint32_t chunk_size) {
    auto pool = MakeBlockMemoryPoolPtr(chunk_size, 4u);
    return std::make_shared<MultiBlockBuffer>(pool);
}

// Snapshot a span's bytes so we can compare them later.
std::vector<uint8_t> Snapshot(const SharedBufferSpan& s) {
    if (!s.Valid()) {
        return {};
    }
    return std::vector<uint8_t>(s.GetStart(), s.GetEnd());
}

}  // namespace

// =============================================================================
//                         SingleBlockBuffer scenarios
// =============================================================================

// -----------------------------------------------------------------------------
// B4-01: Clear() does not perturb bytes covered by an outstanding span.
// -----------------------------------------------------------------------------
// Even though Clear() resets the buffer's read/write pointers to the write
// floor (so future Write()s land *after* the span), the span's bytes must be
// byte-identical before and after the Clear.
TEST(BufferInvariant4_Lifetime, SingleBuffer_ClearDoesNotMutateSpanBytes) {
    auto buf = MakeSingleBuffer(/*chunk_size=*/64u);
    auto p = Pattern(20);
    buf->Write(p.data(), p.size());

    auto span = buf->GetSharedReadableSpan(20u);
    ASSERT_TRUE(span.Valid());
    auto before = Snapshot(span);

    buf->Clear();

    auto after = Snapshot(span);
    EXPECT_EQ(before, after)
        << "Clear() must not mutate the bytes of any live SharedBufferSpan; "
        << "the write floor is supposed to keep the span's range frozen.";
    EXPECT_EQ(p, after)
        << "Snapshot should also still match the originally written payload.";
}

// -----------------------------------------------------------------------------
// B4-02: Subsequent Write() after Clear() lands beyond the span (write floor).
// -----------------------------------------------------------------------------
// We want to make sure that the post-Clear Write() actually does end up
// writing *new* bytes (not silently dropped because the floor leaves no
// room) and that those new bytes are *outside* the span's range. This is the
// concrete proof that floor-based protection is functional, not merely a
// coincidence of the test setup.
TEST(BufferInvariant4_Lifetime, SingleBuffer_WriteAfterClearGoesPastSpan) {
    auto buf = MakeSingleBuffer(/*chunk_size=*/64u);
    auto p1 = Pattern(20, 0x10);
    buf->Write(p1.data(), p1.size());

    auto span = buf->GetSharedReadableSpan(20u);
    ASSERT_TRUE(span.Valid());
    auto* span_end = span.GetEnd();
    auto before = Snapshot(span);

    buf->Clear();

    // Now write fresh data; this MUST appear *after* span_end.
    auto p2 = Pattern(8, 0x70);
    auto written = buf->Write(p2.data(), p2.size());
    ASSERT_EQ(p2.size(), written);

    // The span's bytes are still p1.
    EXPECT_EQ(before, Snapshot(span));

    // The fresh bytes lie at or beyond the span's end (in pointer terms).
    auto fresh = buf->GetSharedReadableSpan(static_cast<uint32_t>(p2.size()));
    ASSERT_TRUE(fresh.Valid());
    EXPECT_GE(fresh.GetStart(), span_end)
        << "Post-Clear Write() must not land inside the still-alive span.";
    EXPECT_EQ(0, std::memcmp(fresh.GetStart(), p2.data(), p2.size()));
}

// -----------------------------------------------------------------------------
// B4-03: Span survives the buffer being dropped entirely.
// -----------------------------------------------------------------------------
// The classic "park on async send queue" scenario: the buffer object is
// reset, but the chunk lives on through the span's shared_ptr, and its
// bytes remain readable and unchanged.
TEST(BufferInvariant4_Lifetime, SingleBuffer_SpanOutlivesBufferDestruction) {
    auto buf = MakeSingleBuffer(/*chunk_size=*/64u);
    auto p = Pattern(16, 0x20);
    buf->Write(p.data(), p.size());

    auto span = buf->GetSharedReadableSpan(16u);
    ASSERT_TRUE(span.Valid());
    auto chunk_alive = span.GetChunk();  // explicit witness for chunk lifetime
    ASSERT_TRUE(chunk_alive != nullptr);

    // Drop the buffer.
    buf.reset();

    ASSERT_TRUE(span.Valid())
        << "Span must remain valid after its originating buffer is destroyed.";
    EXPECT_EQ(p.size(), span.GetLength());
    EXPECT_EQ(0, std::memcmp(span.GetStart(), p.data(), p.size()));
}

// -----------------------------------------------------------------------------
// B4-04: MoveReadPt() / Read() do not perturb already-issued span bytes.
// -----------------------------------------------------------------------------
// Read pointer movement only affects what *future* reads see; the bytes the
// span points to (which already lie behind read_pos_ in offset terms) must
// remain byte-stable.
TEST(BufferInvariant4_Lifetime, SingleBuffer_MoveReadPtDoesNotMutateSpan) {
    auto buf = MakeSingleBuffer(/*chunk_size=*/64u);
    auto p = Pattern(24, 0x30);
    buf->Write(p.data(), p.size());

    auto span = buf->GetSharedReadableSpan(24u);
    ASSERT_TRUE(span.Valid());
    auto before = Snapshot(span);

    // Drain the read pointer past the span.
    auto moved = buf->MoveReadPt(24u);
    EXPECT_EQ(24u, moved);
    EXPECT_EQ(0u, buf->GetDataLength());

    EXPECT_EQ(before, Snapshot(span))
        << "Advancing the read pointer must not retroactively rewrite bytes "
        << "in a still-living span.";
}

// -----------------------------------------------------------------------------
// B4-05: Repeated Clear+Write cycles never overwrite a held span.
// -----------------------------------------------------------------------------
// Stress-tests state churn: we keep clearing and re-writing while a single
// span sits around. As long as the span lives, the original bytes survive.
TEST(BufferInvariant4_Lifetime, SingleBuffer_ChurnCyclesPreserveSpan) {
    auto buf = MakeSingleBuffer(/*chunk_size=*/64u);
    auto original = Pattern(20, 0x10);
    buf->Write(original.data(), original.size());

    auto span = buf->GetSharedReadableSpan(20u);
    ASSERT_TRUE(span.Valid());
    auto before = Snapshot(span);

    for (int i = 0; i < 5; ++i) {
        buf->Clear();
        auto fresh = Pattern(static_cast<uint32_t>(8 + i), static_cast<uint8_t>(0x80 + i));
        // Some iterations may have less free space than requested because the
        // floor consumes part of the chunk. That is fine — what we care about
        // is that *whatever* gets written never disturbs the held span.
        buf->Write(fresh.data(), static_cast<uint32_t>(fresh.size()));
    }

    EXPECT_EQ(before, Snapshot(span))
        << "Churn (Clear + Write) must not perturb the held span at any cycle.";
    EXPECT_EQ(original, Snapshot(span));
}

// =============================================================================
//                         MultiBlockBuffer scenarios
// =============================================================================

// -----------------------------------------------------------------------------
// B4-06: MultiBlockBuffer::Clear() releases its chunk_ deque but live spans
//        keep their chunks alive and their bytes intact.
// -----------------------------------------------------------------------------
// MultiBlockBuffer's Clear() simply drops its internal deque<ChunkState>.
// Each held SharedBufferSpan owns its chunk through shared_ptr, so the chunk
// memory survives. Crucially, MultiBlockBuffer never reuses a chunk after
// Clear() — it always allocates fresh ones from the pool — so the issued
// span's bytes are guaranteed unmolested.
TEST(BufferInvariant4_Lifetime, MultiBuffer_ClearLeavesSpanIntact) {
    auto buf = MakeMultiBuffer(/*chunk_size=*/16u);
    auto p1 = Pattern(10, 0x10);
    auto p2 = Pattern(10, 0x40);
    buf->Write(p1.data(), p1.size());
    buf->Write(p2.data(), p2.size());

    // Grab a span on the *first* chunk.
    auto span = buf->GetFirstChunkReadable(/*max_length=*/16u);
    ASSERT_TRUE(span.Valid());
    auto before = Snapshot(span);

    buf->Clear();
    EXPECT_EQ(0u, buf->GetDataLength());

    // Span still points at the original bytes.
    EXPECT_EQ(before, Snapshot(span))
        << "MultiBlockBuffer::Clear() must not corrupt outstanding span bytes.";
}

// -----------------------------------------------------------------------------
// B4-07: After Clear(), a follow-up Write() goes into a fresh chunk and does
//        not collide with the span's chunk.
// -----------------------------------------------------------------------------
// The deque is empty after Clear(); the next Write() must allocate (or
// fetch from the pool) a *different* chunk. The span and the new write
// therefore live in disjoint memory regions and cannot interfere.
TEST(BufferInvariant4_Lifetime, MultiBuffer_ClearThenWriteUsesDistinctChunk) {
    auto buf = MakeMultiBuffer(/*chunk_size=*/16u);
    auto p1 = Pattern(10, 0x10);
    buf->Write(p1.data(), p1.size());

    auto span = buf->GetFirstChunkReadable(/*max_length=*/16u);
    ASSERT_TRUE(span.Valid());
    auto* span_chunk_raw = span.GetChunk().get();
    auto before = Snapshot(span);

    buf->Clear();

    auto p2 = Pattern(8, 0xC0);
    buf->Write(p2.data(), p2.size());

    auto fresh = buf->GetFirstChunkReadable(/*max_length=*/16u);
    ASSERT_TRUE(fresh.Valid());

    // Either the implementation allocated a brand-new chunk, OR it
    // legitimately reused a fresh slot inside an unused chunk that was never
    // referenced by the held span. The contract: the new chunk must not be
    // the same chunk object the held span pinned, OR if it is, the new write
    // must land *past* the span's end (write-floor protection).
    if (fresh.GetChunk().get() == span_chunk_raw) {
        EXPECT_GE(fresh.GetStart(), span.GetEnd())
            << "If a chunk is reused, the write floor must keep the new "
               "bytes outside the span's range.";
    }
    EXPECT_EQ(before, Snapshot(span))
        << "Span bytes must not be mutated regardless of which chunk the "
           "new write landed in.";
    EXPECT_EQ(0, std::memcmp(fresh.GetStart(), p2.data(), p2.size()));
}

// -----------------------------------------------------------------------------
// B4-08: Span outlives the entire MultiBlockBuffer being destroyed.
// -----------------------------------------------------------------------------
// Same as B4-03 but for the multi-chunk variant. The chunk lives on via
// shared_ptr inside the span; tearing down the buffer is harmless.
TEST(BufferInvariant4_Lifetime, MultiBuffer_SpanOutlivesBufferDestruction) {
    auto buf = MakeMultiBuffer(/*chunk_size=*/16u);
    auto p = Pattern(12, 0x55);
    buf->Write(p.data(), p.size());

    auto span = buf->GetFirstChunkReadable(/*max_length=*/16u);
    ASSERT_TRUE(span.Valid());
    auto chunk_alive = span.GetChunk();
    ASSERT_TRUE(chunk_alive != nullptr);

    buf.reset();

    ASSERT_TRUE(span.Valid())
        << "Span must remain valid after its MultiBlockBuffer is destroyed.";
    EXPECT_EQ(p.size(), span.GetLength());
    EXPECT_EQ(0, std::memcmp(span.GetStart(), p.data(), p.size()));
}

// -----------------------------------------------------------------------------
// B4-09: Multiple coexisting spans across Clear cycles all stay byte-stable.
// -----------------------------------------------------------------------------
// Two spans issued at different times must both survive a Clear unchanged.
// This catches any logic that accidentally only protects the most recently
// issued span.
TEST(BufferInvariant4_Lifetime, MultiBuffer_MultipleSpansAllSurviveClear) {
    auto buf = MakeMultiBuffer(/*chunk_size=*/8u);
    auto p1 = Pattern(8, 0x10);
    auto p2 = Pattern(8, 0x40);
    auto p3 = Pattern(8, 0x80);
    buf->Write(p1.data(), p1.size());
    buf->Write(p2.data(), p2.size());
    buf->Write(p3.data(), p3.size());
    ASSERT_EQ(3u, buf->GetChunkCount());

    auto spans = buf->GetSharedReadableSpans(/*length=*/24u);
    ASSERT_EQ(3u, spans.size());

    std::vector<std::vector<uint8_t>> snaps;
    snaps.reserve(spans.size());
    for (const auto& s : spans) {
        snaps.emplace_back(Snapshot(s));
    }

    buf->Clear();
    EXPECT_EQ(0u, buf->GetDataLength());

    // Drop the source buffer too, for good measure.
    buf.reset();

    for (size_t i = 0; i < spans.size(); ++i) {
        ASSERT_TRUE(spans[i].Valid()) << "span[" << i << "] should remain valid";
        EXPECT_EQ(snaps[i], Snapshot(spans[i]))
            << "span[" << i << "] bytes drifted after Clear() + buffer drop.";
    }

    // First chunk should be the p1 pattern, second p2, third p3.
    EXPECT_EQ(p1, snaps[0]);
    EXPECT_EQ(p2, snaps[1]);
    EXPECT_EQ(p3, snaps[2]);
}

}  // namespace common
}  // namespace quicx
