// Bounds tests for the long-header decrypt path (Initial / Handshake / 0-RTT).
//
// These packets are processed before the peer is authenticated -- and for
// Initial, before any secret is shared at all, since Initial keys derive from
// the public DCID (RFC 9001 §5.2). Anyone able to send a UDP datagram reaches
// this code, so every length taken off the wire has to be checked.
//
// Two unchecked reads existed on all three long-header types, while the short
// header (rtt_1_packet.cpp:128) already guarded the first:
//
//   1. The header-protection sample (RFC 9001 §5.4.2) was built as
//        [start + pn_offset + 4, start + pn_offset + 4 + 16)
//      with no check that 16 bytes are present, then handed to DecryptHeader ->
//      MakeHeaderProtectMask -> EVP_EncryptUpdate(..., sample.GetStart(), 16),
//      which reads all 16. Measured on a 25-byte packet in a 25-byte chunk: the
//      sample ran 12 bytes past the end of the allocation.
//
//   2. The payload span was built as `cur_pos + length_ - packet_num_len`.
//      DecodeWithoutCrypto bounds length_ only from above (init_packet.cpp:141,
//      `cur_pos + length_ > end`), never checking it is at least
//      packet_num_len, so a small length_ underflows the unsigned subtraction
//      into a ~2^64-byte span. Reachable in both branches of DecodeWithCrypto,
//      including the no-cryptographer branch, which needs no keys.
//
// ---------------------------------------------------------------------------
// Why these are behavioural tests rather than sanitizer tests
// ---------------------------------------------------------------------------
// The overread happens *inside BoringSSL* (EVP_EncryptUpdate). BoringSSL is
// built separately and is not instrumented, so ASan cannot see it -- verified by
// running these cases under -DSANITIZER=asan and getting no report while a
// printf probe showed the sample pointer12 bytes past the chunk. Nor can the
// return value distinguish: before the fix the call stillended in `false`,
// just via an AEAD authentication failure *after* the bad read.
//
// So the assertion is on the property that actually matters -- an unvalidated
// pointer must never reach the cryptographer at all. A spy records whether
// DecryptHeader was invoked. Before the fix it is; after, the packet is rejected
// first. That distinction is also the reason fuzzing missed this, on top of the
// harness bug noted in init_packet_fuzz.cpp.

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"
#include "common/decode/decode.h"
#include "quic/packet/handshake_packet.h"
#include "quic/packet/init_packet.h"
#include "quic/packet/rtt_0_packet.h"

#include "test/unit_test/quic/packet/common_test_frame.h"

namespace quicx {
namespace quic {
namespace {

// Forwards everything to a real cryptographer, but records whether the header
// decryption entry point was reached. Reaching it means a span derived from
// unvalidated wire lengths was handed to crypto.
class SpyCryptographer: public ICryptographer {
public:
    explicit SpyCryptographer(std::shared_ptr<ICryptographer> inner): inner_(std::move(inner)) {}

    bool header_decrypt_attempted = false;

    Result DecryptHeader(common::BufferSpan& ciphertext, common::BufferSpan& sample, uint32_t pn_offset,
        uint8_t& out_packet_num_len, bool is_short) override {
        header_decrypt_attempted = true;
        return inner_->DecryptHeader(ciphertext, sample, pn_offset, out_packet_num_len, is_short);
    }

    const char* GetName() override { return inner_->GetName(); }
    CryptographerId GetCipherId() override { return inner_->GetCipherId(); }
    Result InstallSecret(const uint8_t* s, size_t n, bool w) override { return inner_->InstallSecret(s, n, w); }
    Result InstallSecretWithVersion(const uint8_t* s, size_t n, bool w, uint32_t v) override {
        return inner_->InstallSecretWithVersion(s, n, w, v);
    }
    Result InstallInitSecret(const uint8_t* s, size_t n, const uint8_t* salt, size_t sl, bool srv) override {
        return inner_->InstallInitSecret(s, n, salt, sl, srv);
    }
    Result InstallInitSecretWithVersion(const uint8_t* s, size_t n, uint32_t v, bool srv) override {
        return inner_->InstallInitSecretWithVersion(s, n, v, srv);
    }
    Result DecryptPacket(uint64_t pn, common::BufferSpan& ad, common::BufferSpan& ct,
        std::shared_ptr<common::IBuffer> out) override {
        return inner_->DecryptPacket(pn, ad, ct, out);
    }
    Result DecryptPacketWithPrevKey(uint64_t pn, common::BufferSpan& ad, common::BufferSpan& ct,
        std::shared_ptr<common::IBuffer> out) override {
        return inner_->DecryptPacketWithPrevKey(pn, ad, ct, out);
    }
    Result EncryptPacket(uint64_t pn, common::BufferSpan& ad, common::BufferSpan& pt,
        std::shared_ptr<common::IBuffer> out) override {
        return inner_->EncryptPacket(pn, ad, pt, out);
    }
    bool HasPrevReadKey() const override { return inner_->HasPrevReadKey(); }
    Result EncryptHeader(common::BufferSpan& pt, common::BufferSpan& sample, uint32_t pn_offset, size_t pn_len,
        bool is_short) override {
        return inner_->EncryptHeader(pt, sample, pn_offset, pn_len, is_short);
    }
    size_t GetTagLength() override { return inner_->GetTagLength(); }
    Result KeyUpdate(const uint8_t* s, size_t n, bool w) override { return inner_->KeyUpdate(s, n, w); }
    Result KeyUpdateWithVersion(const uint8_t* s, size_t n, bool w, uint32_t v) override {
        return inner_->KeyUpdateWithVersion(s, n, w, v);
    }
    void SetVersion(uint32_t v) override { inner_->SetVersion(v); }
    uint32_t GetVersion() const override { return inner_->GetVersion(); }

private:
    std::shared_ptr<ICryptographer> inner_;
};

// Sized to fit |len| exactly, so the packet ends where the allocation ends.
std::shared_ptr<common::SingleBlockBuffer> MakeBuffer(const uint8_t* data, size_t len) {
    auto buffer = std::make_shared<common::SingleBlockBuffer>(
        std::make_shared<common::StandaloneBufferChunk>(static_cast<uint32_t>(len)));
    buffer->Write(data, static_cast<uint32_t>(len));
    return buffer;
}

// Long header with an empty token, a caller-chosen `length` field, and
// |payload_bytes| bytes actually present. |length| is written verbatim so a test
// can make it disagree with reality -- that disagreement is the point.
std::vector<uint8_t> BuildLongHeaderPacket(
    uint8_t first_byte, uint64_t length, size_t payload_bytes, bool with_token) {
    std::vector<uint8_t> raw;
    raw.push_back(first_byte);
    raw.insert(raw.end(), {0x00, 0x00, 0x00, 0x01});// version 1
    raw.insert(raw.end(), {0x04, 0x01, 0x02, 0x03, 0x04});  // DCID len + DCID
    raw.insert(raw.end(), {0x04, 0x05, 0x06, 0x07, 0x08});  // SCID len + SCID
    if (with_token) {
        raw.push_back(0x00);  // token length varint = 0 (Initial only)
    }

    uint8_t tmp[8] = {0};
    uint8_t* len_end = common::EncodeVarint(tmp, tmp + sizeof(tmp), length);
    raw.insert(raw.end(), tmp, len_end);

    for (size_t i = 0; i < payload_bytes; ++i) {
        raw.push_back(static_cast<uint8_t>(0xA0 + (i & 0x0F)));
    }
    return raw;
}

std::shared_ptr<SpyCryptographer> MakeSpy() {
    return std::make_shared<SpyCryptographer>(PacketTest::Instance().GetTestServerCryptographer());
}

// ===== Defect 1: sample must be inside the packet before crypto sees it =====

TEST(LongHeaderBoundsTest, InitPacketRejectsShortPayloadBeforeTouchingCrypto) {
    // 8 payload bytes cannot supply the 4 + 16 the sample needs.
    const auto raw = BuildLongHeaderPacket(0xC3, /*length=*/8, /*payload_bytes=*/8, /*with_token=*/true);
    auto buffer = MakeBuffer(raw.data(), raw.size());

    InitPacket pkt(raw[0]);
    buffer->MoveReadPt(1);
    ASSERT_TRUE(pkt.DecodeWithoutCrypto(buffer, false)) << "upper-bound check passes; only the lower bound is missing";

    auto spy = MakeSpy();
    pkt.SetCryptographer(spy);

    EXPECT_FALSE(pkt.DecodeWithCrypto(buffer));
    EXPECT_FALSE(spy->header_decrypt_attempted)
        << "an out-of-bounds sample pointer was handed to DecryptHeader; "
           "EVP_EncryptUpdate reads all 16 bytes of it";
}

TEST(LongHeaderBoundsTest, HandshakePacketRejectsShortPayloadBeforeTouchingCrypto) {
    const auto raw = BuildLongHeaderPacket(0xE3, /*length=*/8, /*payload_bytes=*/8, /*with_token=*/false);
    auto buffer = MakeBuffer(raw.data(), raw.size());

    HandshakePacket pkt(raw[0]);
    buffer->MoveReadPt(1);
    ASSERT_TRUE(pkt.DecodeWithoutCrypto(buffer, false));

    auto spy = MakeSpy();
    pkt.SetCryptographer(spy);

    EXPECT_FALSE(pkt.DecodeWithCrypto(buffer));
    EXPECT_FALSE(spy->header_decrypt_attempted);
}

TEST(LongHeaderBoundsTest, Rtt0PacketRejectsShortPayloadBeforeTouchingCrypto) {
    const auto raw = BuildLongHeaderPacket(0xD3, /*length=*/8, /*payload_bytes=*/8, /*with_token=*/false);
    auto buffer = MakeBuffer(raw.data(), raw.size());

    Rtt0Packet pkt(raw[0]);
    buffer->MoveReadPt(1);
    ASSERT_TRUE(pkt.DecodeWithoutCrypto(buffer, false));

    auto spy = MakeSpy();
    pkt.SetCryptographer(spy);

    EXPECT_FALSE(pkt.DecodeWithCrypto(buffer));
    EXPECT_FALSE(spy->header_decrypt_attempted);
}

// The bound must be >=, not >: exactly one byte short must still be refused.
TEST(LongHeaderBoundsTest, InitPacketRejectsSampleOffByOne) {
    const size_t kNeed = 4 + 16;
    const auto raw = BuildLongHeaderPacket(0xC3, kNeed - 1, /*payload_bytes=*/kNeed - 1, /*with_token=*/true);
    auto buffer = MakeBuffer(raw.data(), raw.size());

    InitPacket pkt(raw[0]);
    buffer->MoveReadPt(1);
    ASSERT_TRUE(pkt.DecodeWithoutCrypto(buffer, false));

    auto spy = MakeSpy();
    pkt.SetCryptographer(spy);

    EXPECT_FALSE(pkt.DecodeWithCrypto(buffer));
    EXPECT_FALSE(spy->header_decrypt_attempted);
}

// A payload long enough for the sample must still reach crypto, or the bound is
// too strict and would break legitimate traffic.
TEST(LongHeaderBoundsTest, InitPacketWithSufficientPayloadStillReachesCrypto) {
    const auto raw = BuildLongHeaderPacket(0xC3, /*length=*/40, /*payload_bytes=*/40, /*with_token=*/true);
    auto buffer = MakeBuffer(raw.data(), raw.size());

    InitPacket pkt(raw[0]);
    buffer->MoveReadPt(1);
    ASSERT_TRUE(pkt.DecodeWithoutCrypto(buffer, false));

    auto spy = MakeSpy();
    pkt.SetCryptographer(spy);

    // Decryption fails (the bytes are not a real packet) but must be attempted.
    pkt.DecodeWithCrypto(buffer);
    EXPECT_TRUE(spy->header_decrypt_attempted);
}

// ===== Defect 2: length_ - packet_num_len must not underflow =====
//
// Uses the no-cryptographer branch: deterministic and needs no keys. The low
// bits of the first byte carry "PN length - 1", so 0xC3 asks for a 4-byte PN
// while length_ = 1.

TEST(LongHeaderBoundsTest, InitPacketRejectsLengthSmallerThanPacketNumberLength) {
    const auto raw = BuildLongHeaderPacket(0xC3, /*length=*/1, /*payload_bytes=*/24, /*with_token=*/true);
    auto buffer = MakeBuffer(raw.data(), raw.size());

    InitPacket pkt(raw[0]);
    buffer->MoveReadPt(1);
    ASSERT_TRUE(pkt.DecodeWithoutCrypto(buffer, false));
    EXPECT_FALSE(pkt.DecodeWithCrypto(buffer));
}

TEST(LongHeaderBoundsTest, HandshakePacketRejectsLengthSmallerThanPacketNumberLength) {
    const auto raw = BuildLongHeaderPacket(0xE3, /*length=*/1, /*payload_bytes=*/24, /*with_token=*/false);
    auto buffer = MakeBuffer(raw.data(), raw.size());

    HandshakePacket pkt(raw[0]);
    buffer->MoveReadPt(1);
    ASSERT_TRUE(pkt.DecodeWithoutCrypto(buffer, false));
    EXPECT_FALSE(pkt.DecodeWithCrypto(buffer));
}

TEST(LongHeaderBoundsTest, Rtt0PacketRejectsLengthSmallerThanPacketNumberLength) {
    const auto raw = BuildLongHeaderPacket(0xD3, /*length=*/1, /*payload_bytes=*/24, /*with_token=*/false);
    auto buffer = MakeBuffer(raw.data(), raw.size());

    Rtt0Packet pkt(raw[0]);
    buffer->MoveReadPt(1);
    ASSERT_TRUE(pkt.DecodeWithoutCrypto(buffer, false));
    EXPECT_FALSE(pkt.DecodeWithCrypto(buffer));
}

}  // namespace
}  // namespace quic
}  // namespace quicx
