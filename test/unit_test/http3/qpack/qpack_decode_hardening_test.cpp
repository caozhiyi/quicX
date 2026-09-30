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
//
// The same "resize first, read second" pattern also existed in two more places
// that did NOT go through the hardened helper — QpackEncoder::DecodeString()
// (every literal header field's *value*) and QpackEncoder::DecodeLiteralNoName-
// Ref() (its *name*). All three now funnel through QpackReadStringBody(), so
// the bound is enforced in exactly one place; the tests at the bottom of this
// file cover the encoder entry point as well.

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

#include "http3/qpack/qpack_constants.h"
#include "http3/qpack/qpack_encoder.h"
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

// ===== QpackReadStringBody: the single bounded implementation =====
//
// Every string-literal decoder now routes through this helper, so it is the
// one place the "bound the length before allocating" contract has to hold.

TEST(QpackDecodeHardeningTest, ReadStringBodyRejectsLengthBeyondBuffer) {
    auto buf = MakeBuffer();
    ASSERT_TRUE(QpackEncodePrefixedInteger(buf, 7, 0x00, 0x100000000ULL));
    const uint8_t body[] = {'a'};
    buf->Write(body, 1);

    // The helper is the tail of a decoder: the length varint has already been
    // consumed by the caller.
    uint8_t first = 0;
    uint64_t len = 0;
    ASSERT_TRUE(QpackDecodePrefixedInteger(buf, 7, first, len));

    std::string out;
    EXPECT_FALSE(QpackReadStringBody(buf, len, /*huffman=*/false, out));
    EXPECT_TRUE(out.empty());
}

TEST(QpackDecodeHardeningTest, ReadStringBodyRejectsNullBuffer) {
    std::string out;
    EXPECT_FALSE(QpackReadStringBody(nullptr, 16, /*huffman=*/false, out));
}

TEST(QpackDecodeHardeningTest, ReadStringBodyAcceptsExactFit) {
    auto buf = MakeBuffer();
    ASSERT_TRUE(QpackEncodePrefixedInteger(buf, 7, 0x00, 5));
    const uint8_t body[] = {'h', 'e', 'l', 'l', 'o'};
    buf->Write(body, 5);

    uint8_t first = 0;
    uint64_t len = 0;
    ASSERT_TRUE(QpackDecodePrefixedInteger(buf, 7, first, len));
    ASSERT_EQ(len, 5u);

    std::string out;
    ASSERT_TRUE(QpackReadStringBody(buf, len, /*huffman=*/false, out));
    EXPECT_EQ(out, "hello");
}

// ===== QpackEncoder::Decode: the end-to-end header-block path =====
//
// Before the fix, a single HEADERS frame claiming a 4 GiB literal reached
// std::string::resize() and threw std::bad_alloc out of the HTTP/3 stream
// callback, terminating the process. It must now simply fail to decode.

TEST(QpackDecodeHardeningTest, EncoderRejectsOversizedLiteralValue) {
    QpackEncoder encoder;

    auto buf = MakeBuffer(256);
    // Header block prefix: Required Insert Count = 0, Delta Base = 0.
    const uint8_t prefix[] = {0x00, 0x00};
    ASSERT_EQ(buf->Write(prefix, 2), 2u);
    // Literal Field Line With Literal Name (001 N H NameLen): N=0, H=0, name len 0.
    const uint8_t literal_no_name_ref = QpackHeaderPattern::kLiteralNoNameRef;  // 001x xxxx
    ASSERT_EQ(buf->Write(&literal_no_name_ref, 1), 1u);
    // Value length: claims 4 GiB, with nothing behind it.
    ASSERT_TRUE(QpackEncodePrefixedInteger(buf, QpackString::kLengthPrefix, 0x00, 0x100000000ULL));

    std::vector<std::pair<std::string, std::string>> headers;
    EXPECT_FALSE(encoder.Decode(buf, headers));
    EXPECT_TRUE(headers.empty());
}

TEST(QpackDecodeHardeningTest, EncoderRejectsOversizedLiteralName) {
    QpackEncoder encoder;

    auto buf = MakeBuffer(256);
    const uint8_t prefix[] = {0x00, 0x00};
    ASSERT_EQ(buf->Write(prefix, 2), 2u);
    // Literal Field Line With Literal Name, 3-bit name-length prefix set to all
    // ones so a continuation-encoded (huge) name length follows.
    const uint8_t name_len_continuation = QpackHeaderPattern::kLiteralNoNameRef | 0x07;
    ASSERT_EQ(buf->Write(&name_len_continuation, 1), 1u);
    // Continuation bytes encoding a 4 GiB name with no payload behind them.
    const uint8_t huge[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x0F};
    ASSERT_EQ(buf->Write(huge, sizeof(huge)), sizeof(huge));

    std::vector<std::pair<std::string, std::string>> headers;
    EXPECT_FALSE(encoder.Decode(buf, headers));
    EXPECT_TRUE(headers.empty());
}

// A well-formed block must still decode — the bound must not reject valid input.
TEST(QpackDecodeHardeningTest, EncoderAcceptsWellFormedLiteral) {
    QpackEncoder encoder;

    auto buf = MakeBuffer(256);
    const uint8_t prefix[] = {0x00, 0x00};
    ASSERT_EQ(buf->Write(prefix, 2), 2u);
    // Name length 4 (fits the 3-bit prefix? no: 4 > 7? yes 4 <= 7 so it fits).
    const uint8_t name_len = QpackHeaderPattern::kLiteralNoNameRef | 0x04;
    ASSERT_EQ(buf->Write(&name_len, 1), 1u);
    const uint8_t name[] = {'n', 'a', 'm', 'e'};
    ASSERT_EQ(buf->Write(name, sizeof(name)), sizeof(name));
    ASSERT_TRUE(QpackEncodePrefixedInteger(buf, QpackString::kLengthPrefix, 0x00, 5));
    const uint8_t value[] = {'v', 'a', 'l', 'u', 'e'};
    ASSERT_EQ(buf->Write(value, sizeof(value)), sizeof(value));

    std::vector<std::pair<std::string, std::string>> headers;
    ASSERT_TRUE(encoder.Decode(buf, headers));
    ASSERT_EQ(headers.size(), 1u);
    EXPECT_EQ(headers[0].first, "name");
    EXPECT_EQ(headers[0].second, "value");
}

// ===== Round-trip faithfulness (found by qpack_roundtrip_fuzz) =====

// The fuzzer found that Encode -> Decode was not byte-faithful in two ways
// (both fixed); these tests pin the fixes. A shared harness mirrors the real
// connection wiring: two QpackEncoder instances with the encoder-instruction
// and insert-count side channels bridged the way the QPACK streams do.
namespace roundtrip_fidelity {

using FieldList = std::vector<std::pair<std::string, std::string>>;

struct Pair {
    QpackEncoder enc;
    QpackEncoder dec;
    Pair(uint32_t cap) {
        enc.SetLocalMaxTableCapacity(cap);
        enc.SetPeerMaxTableCapacity(cap);
        enc.SetDynamicTableEnabled(cap > 0);
        dec.SetMaxTableCapacity(cap);
        dec.SetDynamicTableEnabled(cap > 0);
        QpackEncoder* enc_p = &enc;
        QpackEncoder* dec_p = &dec;
        enc.SetInstructionSender([enc_p, dec_p](const FieldList& inserts) {
            QpackEncoder scratch;
            auto instr = MakeBuffer(64 + inserts.size() * 600);
            if (!scratch.EncodeEncoderInstructions(inserts, instr)) {
                return;
            }
            uint64_t before = dec_p->GetInsertCount();
            if (dec_p->DecodeEncoderInstructions(instr)) {
                uint64_t delta = dec_p->GetInsertCount() - before;
                if (delta > 0) {
                    enc_p->OnPeerInsertCountIncrement(delta);
                }
            }
        });
    }
};

FieldList Sorted(FieldList fields) {
    std::sort(fields.begin(), fields.end());
    return fields;
}

// Fuzz crash #1: an unknown ':'-prefixed name (":", not one of the five
// known pseudo-headers) was silently DROPPED by OrderHeaders, and duplicate
// known pseudo-headers were de-duplicated first-wins. QPACK is a
// byte-faithful compression layer; semantic validation belongs above.
TEST(QpackRoundtripFidelityTest, UnknownPseudoHeaderAndDuplicateKept) {
    Pair p(0);  // dynamic table disabled: pure static/literal path
    FieldList in = {{":", ""}, {":method", "GET"}, {":method", "POST"}, {"x-a", "1"}};
    auto wire = MakeBuffer(512);
    ASSERT_TRUE(p.enc.Encode(in, wire));

    FieldList back;
    ASSERT_TRUE(p.dec.Decode(wire, back));
    EXPECT_EQ(Sorted(back), Sorted(in)) << "every field line must survive the round trip";
}

// Fuzz crash #2: a mixed-case name (":methOd") was lowercased before the
// static-table name-only match, so it came back as ":method". Legal traffic
// is all-lowercase and unaffected, but the compression layer must not
// rewrite bytes.
TEST(QpackRoundtripFidelityTest, MixedCaseNameIsNotLowercased) {
    Pair p(0);
    FieldList in = {{":methOd", ""}, {"Content-Type", "text/html"}};
    auto wire = MakeBuffer(512);
    ASSERT_TRUE(p.enc.Encode(in, wire));

    FieldList back;
    ASSERT_TRUE(p.dec.Decode(wire, back));
    ASSERT_EQ(back.size(), 2u);
    EXPECT_EQ(Sorted(back), Sorted(in)) << "names must round-trip byte-faithfully, not lowercased";
}

}  // namespace roundtrip_fidelity

}  // namespace
}  // namespace http3
}  // namespace quicx
