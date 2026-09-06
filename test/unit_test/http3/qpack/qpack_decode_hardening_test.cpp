// Hardening tests for the QPACK integer/string-literal decoders.
//
// Both decoders read attacker-controlled bytes off a peer's QPACK stream, so a
// malformed encoding must be rejected rather than trusted:
//
//   * QpackDecodePrefixedInteger accumulated shifts with `m += 7` and no bound.
//     Once m reaches 64, `<< m` on a uint64_t is undefined behaviour, and a
//     continuation run also silently wrapped the accumulated value.
//
//   * QpackDecodeStringLiteral called out.resize(len) with a 64-bit len taken
//     straight from the wire, before reading anything. A 5-byte header could
//     request a multi-gigabyte allocation. Worse, the success check compared a
//     uint32_t-truncated read count against an int32_t-truncated length, so
//     len = 0x100000000 read 0 bytes, compared 0 == 0, and reported success
//     holding a 4 GiB string.

#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

#include "http3/qpack/util.h"

namespace quicx {
namespace http3 {
namespace {

std::shared_ptr<common::SingleBlockBuffer> MakeBuffer(size_t cap = 1024) {
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(cap);
    return std::make_shared<common::SingleBlockBuffer>(chunk);
}

std::shared_ptr<common::SingleBlockBuffer> BufferOf(const std::vector<uint8_t>& bytes) {
    auto buf = MakeBuffer(bytes.size() + 16);
    buf->Write(bytes.data(), static_cast<uint32_t>(bytes.size()));
    return buf;
}

// ===== Prefixed integer: continuation-run abuse =====

// 10 continuation bytes drive the shift to 70. Reject rather than shift by >= 64.
TEST(QpackDecodeHardeningTest, RejectsOverlongContinuationRun) {
    std::vector<uint8_t> bytes = {0xFF};  // prefix all-ones -> continuation follows
    for (int i = 0; i < 10; ++i) {
        bytes.push_back(0x80);  // continuation bit set, payload 0
    }
    bytes.push_back(0x01);  // terminator

    auto buf = BufferOf(bytes);
    uint8_t first = 0;
    uint64_t value = 0;
    EXPECT_FALSE(QpackDecodePrefixedInteger(buf, 8, first, value));
}

// A continuation run that would carry the value past 2^64.
TEST(QpackDecodeHardeningTest, RejectsValueOverflowingUint64) {
    std::vector<uint8_t> bytes = {0xFF};
    for (int i = 0; i < 9; ++i) {
        bytes.push_back(0xFF);  // continuation set, payload 0x7f each
    }
    bytes.push_back(0x7F);

    auto buf = BufferOf(bytes);
    uint8_t first = 0;
    uint64_t value = 0;
    EXPECT_FALSE(QpackDecodePrefixedInteger(buf, 8, first, value));
}

// Truncated input (continuation bit set, nothing follows) must fail, not spin.
TEST(QpackDecodeHardeningTest, RejectsTruncatedContinuation) {
    auto buf = BufferOf({0xFF, 0x80});
    uint8_t first = 0;
    uint64_t value = 0;
    EXPECT_FALSE(QpackDecodePrefixedInteger(buf, 8, first, value));
}

// Well-formed values must still round-trip: the bound must not be so tight that
// it rejects legitimate encodings.
TEST(QpackDecodeHardeningTest, AcceptsLegitimateMultiByteInteger) {
    auto buf = MakeBuffer();
    ASSERT_TRUE(QpackEncodePrefixedInteger(buf, 8, 0x00, 1337));

    uint8_t first = 0;
    uint64_t value = 0;
    ASSERT_TRUE(QpackDecodePrefixedInteger(buf, 8, first, value));
    EXPECT_EQ(value, 1337u);
}

TEST(QpackDecodeHardeningTest, AcceptsMaxUint64) {
    auto buf = MakeBuffer();
    ASSERT_TRUE(QpackEncodePrefixedInteger(buf, 8, 0x00, UINT64_MAX));

    uint8_t first = 0;
    uint64_t value = 0;
    ASSERT_TRUE(QpackDecodePrefixedInteger(buf, 8, first, value));
    EXPECT_EQ(value, UINT64_MAX);
}

TEST(QpackDecodeHardeningTest, AcceptsSmallValueInPrefix) {
    auto buf = MakeBuffer();
    ASSERT_TRUE(QpackEncodePrefixedInteger(buf, 7, 0x00, 5));

    uint8_t first = 0;
    uint64_t value = 0;
    ASSERT_TRUE(QpackDecodePrefixedInteger(buf, 7, first, value));
    EXPECT_EQ(value, 5u);
}

// ===== String literal: length must be bounded by what is actually there =====

// The headline case: a 5-byte input claiming a 4 GiB body.
TEST(QpackDecodeHardeningTest, RejectsLengthBeyondBufferInsteadOfAllocating) {
    // 0x7F prefix (7-bit, all ones) then a continuation-encoded 0x100000000.
    auto buf = MakeBuffer();
    ASSERT_TRUE(QpackEncodePrefixedInteger(buf, 7, 0x00, 0x100000000ULL));

    std::string out;
    EXPECT_FALSE(QpackDecodeStringLiteral(buf, out));
    EXPECT_TRUE(out.empty());
}

// Same class, smaller: claims 1 MiB with3 bytes of payload present.
TEST(QpackDecodeHardeningTest, RejectsLengthExceedingRemainingBytes) {
    auto buf = MakeBuffer();
    ASSERT_TRUE(QpackEncodePrefixedInteger(buf, 7, 0x00, 1024 * 1024));
    const uint8_t body[] = {'a', 'b', 'c'};
    buf->Write(body, 3);

    std::string out;
    EXPECT_FALSE(QpackDecodeStringLiteral(buf, out));
}

// Off-by-one: exactly one byte more than available.
TEST(QpackDecodeHardeningTest, RejectsLengthOneByteTooLong) {
    auto buf = MakeBuffer();
    ASSERT_TRUE(QpackEncodePrefixedInteger(buf, 7, 0x00, 4));
    const uint8_t body[] = {'a', 'b', 'c'};
    buf->Write(body, 3);

    std::string out;
    EXPECT_FALSE(QpackDecodeStringLiteral(buf, out));
}

TEST(QpackDecodeHardeningTest, RejectsHuffmanFlaggedLengthBeyondBuffer) {
    // Huffman bit is the MSB of the length prefix byte; the bound must apply
    // on that path too.
    auto buf = MakeBuffer();
    ASSERT_TRUE(QpackEncodePrefixedInteger(buf, 7, 0x00, 1024 * 1024));
    // Flip the Huffman bit in the first byte we just wrote.
    auto span = buf->GetReadableSpan();
    ASSERT_GT(span.GetLength(), 0u);
    span.GetStart()[0] |= 0x80;

    std::string out;
    EXPECT_FALSE(QpackDecodeStringLiteral(buf, out));
}

// Round-trip must keep working, including the empty string and the exact-fit
// boundary.
TEST(QpackDecodeHardeningTest, AcceptsExactFitLiteral) {
    auto buf = MakeBuffer();
    ASSERT_TRUE(QpackEncodeStringLiteral("hello", buf, /*huffman=*/false));

    std::string out;
    ASSERT_TRUE(QpackDecodeStringLiteral(buf, out));
    EXPECT_EQ(out, "hello");
}

TEST(QpackDecodeHardeningTest, AcceptsEmptyLiteral) {
    auto buf = MakeBuffer();
    ASSERT_TRUE(QpackEncodeStringLiteral("", buf, /*huffman=*/false));

    std::string out;
    ASSERT_TRUE(QpackDecodeStringLiteral(buf, out));
    EXPECT_TRUE(out.empty());
}

TEST(QpackDecodeHardeningTest, AcceptsHuffmanRoundTrip) {
    auto buf = MakeBuffer();
    ASSERT_TRUE(QpackEncodeStringLiteral("www.example.com", buf, /*huffman=*/true));

    std::string out;
    ASSERT_TRUE(QpackDecodeStringLiteral(buf, out));
    EXPECT_EQ(out, "www.example.com");
}

TEST(QpackDecodeHardeningTest, RejectsEmptyBuffer) {
    auto buf = MakeBuffer();
    std::string out;
    EXPECT_FALSE(QpackDecodeStringLiteral(buf, out));
}

}  // namespace
}  // namespace http3
}  // namespace quicx
