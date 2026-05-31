// Tests for zero-copy invariant 3 (cross-segment must be explicit).
//
// The invariant: when a caller asks a buffer for a contiguous SharedBufferSpan
// of length N and the data spans multiple underlying chunks, the buffer MUST
// NOT silently allocate a fresh chunk and memcpy bytes into it. That hides
// allocations on a hot path and presents copies as "zero-copy" to the caller.
//
// Instead the caller must opt in explicitly by either
//   (a) asking for *only* the leading chunk's readable bytes via
//       GetFirstChunkReadable(max), or
//   (b) asking for *all* segments in order via GetSharedReadableSpans(N).
//
// Plain GetSharedReadableSpan(N) MUST refuse to span chunk boundaries: it
// either returns a span confined to the first chunk, or — when the caller
// demands a fixed length that exceeds the first chunk's readable bytes — an
// invalid span. There is no third "magic merge" path.

#include <cstring>
#include <memory>
#include <numeric>
#include <vector>

#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_chunk.h"
#include "common/buffer/multi_block_buffer.h"
#include "common/buffer/shared_buffer_span.h"

namespace quicx {
namespace common {
namespace {

// Build a MultiBlockBuffer whose pool produces fixed-size chunks. Writing
// `payload_total` bytes into it is guaranteed to spill into multiple chunks
// when payload_total > chunk_size.
std::shared_ptr<MultiBlockBuffer> MakeMultiBuffer(uint32_t chunk_size, uint32_t reserve = 4) {
    auto pool = MakeBlockMemoryPoolPtr(chunk_size, reserve);
    return std::make_shared<MultiBlockBuffer>(pool, /*pre_allocate=*/false);
}

std::vector<uint8_t> MakePattern(uint32_t n, uint8_t base = 0) {
    std::vector<uint8_t> v(n);
    for (uint32_t i = 0; i < n; ++i) {
        v[i] = static_cast<uint8_t>(base + i);
    }
    return v;
}

// -----------------------------------------------------------------------------
// B3-01: GetSharedReadableSpan never silently merges across chunks.
// -----------------------------------------------------------------------------
// We write payload across two chunks (5 + 6 bytes, chunk_size = 8). Asking for
// 11 bytes used to trigger an internal memcpy into a freshly allocated chunk
// and report success. Under invariant 3 the call must return either an
// invalid span or a span no longer than the first chunk's readable bytes.
TEST(BufferInvariant3_CrossSegment, GetSharedReadableSpanRefusesCrossChunkMerge) {
    auto buf = MakeMultiBuffer(/*chunk_size=*/8u);
    auto p1 = MakePattern(5, 0x10);   // 5 bytes -> first chunk
    auto p2 = MakePattern(6, 0x40);   // 6 bytes -> overflows into a 2nd chunk
    ASSERT_EQ(5u, buf->Write(p1.data(), p1.size()));
    ASSERT_EQ(6u, buf->Write(p2.data(), p2.size()));
    ASSERT_GE(buf->GetChunkCount(), 2u) << "Test setup failed: expected >=2 chunks";

    auto span = buf->GetSharedReadableSpan(11u);

    if (span.Valid()) {
        // Permitted shape: a truncated span confined to the first chunk.
        EXPECT_LE(span.GetLength(), 8u)
            << "GetSharedReadableSpan must not span past the first chunk's bytes; "
               "an oversized return implies an internal merge/copy (invariant 3 "
               "violation).";
    }
    // If span is invalid that is also fine — it just means the buffer chose
    // the strict-refuse branch instead of the truncate branch.
}

// -----------------------------------------------------------------------------
// B3-02: must_fill_length=true + cross-chunk request -> always invalid.
// -----------------------------------------------------------------------------
// Callers who insist "give me exactly N contiguous bytes" must be told no
// when N would cross chunks; previously this path silently allocated and
// merged.
TEST(BufferInvariant3_CrossSegment, MustFillLengthRefusesCrossChunk) {
    auto buf = MakeMultiBuffer(/*chunk_size=*/8u);
    ASSERT_EQ(5u, buf->Write(MakePattern(5).data(), 5));
    ASSERT_EQ(6u, buf->Write(MakePattern(6, 0x80).data(), 6));
    ASSERT_GE(buf->GetChunkCount(), 2u);

    auto span = buf->GetSharedReadableSpan(11u, /*must_fill_length=*/true);
    EXPECT_FALSE(span.Valid())
        << "must_fill_length must reject a request that would only succeed by "
           "merging chunks (invariant 3).";
}

// -----------------------------------------------------------------------------
// B3-03: GetFirstChunkReadable returns first chunk's readable bytes verbatim.
// -----------------------------------------------------------------------------
// The explicit happy-path API for "send what you can without copying":
// the returned span starts at the buffer's current read pointer in the first
// chunk and contains exactly min(chunk_readable, max_length) bytes.
TEST(BufferInvariant3_CrossSegment, GetFirstChunkReadableHappyPath) {
    auto buf = MakeMultiBuffer(/*chunk_size=*/8u);
    auto p1 = MakePattern(5, 0x10);
    auto p2 = MakePattern(6, 0x40);
    buf->Write(p1.data(), p1.size());
    buf->Write(p2.data(), p2.size());
    // Write() greedily fills the first chunk before allocating the next, so
    // the first chunk now holds 8 bytes (5 from p1 + first 3 of p2).
    ASSERT_GE(buf->GetChunkCount(), 2u);

    auto span = buf->GetFirstChunkReadable(/*max_length=*/100u);
    ASSERT_TRUE(span.Valid());
    EXPECT_EQ(8u, span.GetLength())
        << "First chunk is full (8/8); GetFirstChunkReadable must surface "
           "all of it without copying.";
    // Bytes 0..4 are p1, bytes 5..7 are p2[0..2].
    std::vector<uint8_t> expected;
    expected.insert(expected.end(), p1.begin(), p1.end());
    expected.insert(expected.end(), p2.begin(), p2.begin() + 3);
    EXPECT_EQ(0, std::memcmp(span.GetStart(), expected.data(), expected.size()));
}

// -----------------------------------------------------------------------------
// B3-04: GetFirstChunkReadable honours the max_length cap.
// -----------------------------------------------------------------------------
TEST(BufferInvariant3_CrossSegment, GetFirstChunkReadableHonoursMax) {
    auto buf = MakeMultiBuffer(/*chunk_size=*/16u);
    auto p1 = MakePattern(10, 0x20);
    buf->Write(p1.data(), p1.size());

    auto span = buf->GetFirstChunkReadable(/*max_length=*/4u);
    ASSERT_TRUE(span.Valid());
    EXPECT_EQ(4u, span.GetLength());
    EXPECT_EQ(0, std::memcmp(span.GetStart(), p1.data(), 4u));
}

// -----------------------------------------------------------------------------
// B3-05: GetSharedReadableSpans returns one span per traversed chunk and
// they cover exactly N bytes in order.
// -----------------------------------------------------------------------------
// Explicit cross-chunk readout: callers that do need bytes from multiple
// chunks must use this API, which never copies — it just hands back several
// distinct SharedBufferSpans (one per chunk) the caller can stitch (or feed
// to scatter-gather IO) on its own.
//
// We exact-fill chunks (8 bytes each) by writing 8 + 8 + 8 = 24 bytes so
// chunk boundaries align with payload boundaries and the test reasons about
// segmentation directly rather than having to mirror Write()'s greedy
// chunk-filling behaviour.
TEST(BufferInvariant3_CrossSegment, GetSharedReadableSpansSegmented) {
    auto buf = MakeMultiBuffer(/*chunk_size=*/8u);
    auto p1 = MakePattern(8, 0x10);
    auto p2 = MakePattern(8, 0x40);
    auto p3 = MakePattern(8, 0x80);
    buf->Write(p1.data(), p1.size());
    buf->Write(p2.data(), p2.size());
    buf->Write(p3.data(), p3.size());
    ASSERT_EQ(3u, buf->GetChunkCount());
    ASSERT_EQ(24u, buf->GetDataLength());

    // Ask for 18 bytes -> spans first chunk (8) + second chunk (8) + 2 of third.
    auto spans = buf->GetSharedReadableSpans(/*length=*/18u);
    ASSERT_GE(spans.size(), 2u)
        << "18 bytes straddle at least the first two chunks; expected "
           "multiple spans, not a merged span.";

    // The concatenation of all returned spans must reproduce the requested
    // prefix byte-for-byte and total exactly 18 bytes.
    std::vector<uint8_t> expected;
    expected.insert(expected.end(), p1.begin(), p1.end());
    expected.insert(expected.end(), p2.begin(), p2.end());
    expected.insert(expected.end(), p3.begin(), p3.begin() + 2);  // 8+8+2 = 18

    std::vector<uint8_t> actual;
    uint32_t total = 0;
    for (const auto& s : spans) {
        ASSERT_TRUE(s.Valid());
        actual.insert(actual.end(), s.GetStart(), s.GetEnd());
        total += s.GetLength();
    }
    EXPECT_EQ(18u, total);
    EXPECT_EQ(expected, actual);

    // None of the returned spans aliases bytes from another span (no merge).
    for (size_t i = 1; i < spans.size(); ++i) {
        EXPECT_NE(spans[i - 1].GetChunk().get(), spans[i].GetChunk().get())
            << "Cross-chunk spans must originate from distinct chunks.";
    }
}

// -----------------------------------------------------------------------------
// B3-06: GetSharedReadableSpans does not advance the read pointer.
// -----------------------------------------------------------------------------
// Like the existing readable-span APIs, the explicit segmented readout is a
// view: the buffer's data length is unchanged afterwards, so the caller may
// inspect, then consume via MoveReadPt explicitly.
TEST(BufferInvariant3_CrossSegment, GetSharedReadableSpansIsNonConsuming) {
    auto buf = MakeMultiBuffer(/*chunk_size=*/8u);
    buf->Write(MakePattern(8).data(), 8);
    buf->Write(MakePattern(8, 0x40).data(), 8);
    auto before = buf->GetDataLength();

    auto spans = buf->GetSharedReadableSpans(8u);
    ASSERT_FALSE(spans.empty());

    EXPECT_EQ(before, buf->GetDataLength())
        << "Reading via spans must not advance the read pointer.";
}

// -----------------------------------------------------------------------------
// B3-07: GetSharedReadableSpans clamps to available bytes (no fabrication).
// -----------------------------------------------------------------------------
TEST(BufferInvariant3_CrossSegment, GetSharedReadableSpansClampsToAvailable) {
    auto buf = MakeMultiBuffer(/*chunk_size=*/8u);
    buf->Write(MakePattern(3).data(), 3);

    auto spans = buf->GetSharedReadableSpans(/*length=*/100u);
    uint32_t total = 0;
    for (const auto& s : spans) {
        total += s.GetLength();
    }
    EXPECT_EQ(3u, total)
        << "Asking for more than is buffered must yield exactly the available "
           "bytes (no allocate-and-zero, no fabrication).";
}

}  // namespace
}  // namespace common
}  // namespace quicx
