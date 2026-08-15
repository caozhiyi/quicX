// Tests for Stateless Reset token derivation (RFC 9000 §10.3).
//
// These tests used to target ConnectionIDCoordinator::GenerateStatelessResetToken,
// a CSPRNG draw. That function is gone: a random token cannot work. The sender of
// a Stateless Reset has, by definition, already lost the connection state, so it
// has nothing left to look a random token up in -- it must *recompute* the token
// from the CID carried in the incoming packet. RFC 9000 §10.3.1 spells out the
// fix, and StatelessResetTokenGenerator implements it:
//
//     token = HMAC-SHA256(static_key, connection_id)[0..15]
//
// So the property under test inverts. It is no longer "successive tokens must
// differ" but "the same CID must always give the same token, and only the key
// holder can produce it". The tests below pin both halves, plus the constant-time
// Verify() path that the receive side depends on.

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <set>
#include <vector>

#include "quic/connection/stateless_reset_token_generator.h"

namespace quicx {
namespace quic {
namespace {

constexpr uint32_t kTokenLen = StatelessResetTokenGenerator::kTokenLength;

using Token = std::vector<uint8_t>;

Token TokenFor(const std::vector<uint8_t>& cid) {
    Token out(kTokenLen, 0);
    EXPECT_TRUE(StatelessResetTokenGenerator::Instance().Generate(
        cid.empty() ? nullptr : cid.data(), static_cast<uint8_t>(cid.size()), out.data()));
    return out;
}

std::vector<uint8_t> Cid(uint8_t seed, uint8_t len = 8) {
    std::vector<uint8_t> cid(len);
    for (uint8_t i = 0; i < len; ++i) {
        cid[i] = static_cast<uint8_t>(seed + i);
    }
    return cid;
}

// The property the whole mechanism rests on. A server that has forgotten the
// connection -- restarted, or the connection simply evicted -- receives a packet
// carrying only the CID and must still produce the exact token the peer was told
// at handshake time. The previous random implementation failed this outright,
// which is why a Stateless Reset send path could not be built on it.
TEST(StatelessResetTokenTest, SameCidAlwaysYieldsSameToken) {
    const auto cid = Cid(0x11);
    EXPECT_EQ(TokenFor(cid), TokenFor(cid)) << "token is not reproducible from the CID, so a server that lost "
                                               "connection state could never send a valid Stateless Reset";
}

// Guards against a regression back to rand(): libc's PRNG is fully determined by
// its seed, so re-seeding would perturb any rand()-based derivation. HMAC does
// not care what srand() did.
TEST(StatelessResetTokenTest, DerivationIsIndependentOfLibcPrngState) {
    const auto cid = Cid(0x22);

    std::srand(1);
    const auto first = TokenFor(cid);
    std::srand(9999);
    const auto second = TokenFor(cid);

    EXPECT_EQ(first, second) << "token changed when the libc PRNG state changed, so it is not a pure "
                                "function of (key, CID)";
}

TEST(StatelessResetTokenTest, DifferentCidsYieldDifferentTokens) {
    EXPECT_NE(TokenFor(Cid(0x01)), TokenFor(Cid(0x02)));
}

// A shared prefix must not leak into a shared token prefix: tokens are compared
// whole, and a peer holding one token must learn nothing about another.
TEST(StatelessResetTokenTest, OneBitCidChangeAvalanchesTheToken) {
    auto cid = Cid(0x33);
    const auto before = TokenFor(cid);

    cid[0] ^= 0x01;  // single bit
    const auto after = TokenFor(cid);

    uint32_t differing = 0;
    for (uint32_t i = 0; i < kTokenLen; ++i) {
        differing += (before[i] != after[i]) ? 1 : 0;
    }
    // HMAC-SHA256 changes ~half the output bits; expected differing bytes is
    // ~15.94 of 16. Eight is far below any plausible run of chance and far above
    // what a truncating or prefix-copying implementation would produce.
    EXPECT_GE(differing, 8u) << "flipping one CID bit barely moved the token";
}

TEST(StatelessResetTokenTest, TokenIsNotTheConnectionIdItself) {
    const auto cid = Cid(0x44, kTokenLen);
    const auto token = TokenFor(cid);

    EXPECT_NE(0, memcmp(token.data(), cid.data(), kTokenLen)) << "token is a copy of the CID, so anyone who has "
                                                                 "seen the CID can reset the connection";
    EXPECT_NE(token, Token(kTokenLen, 0)) << "token buffer was left zeroed";
}

// Every byte position must actually be filled by the digest; a partially written
// buffer would leave constant positions an attacker can skip guessing.
TEST(StatelessResetTokenTest, EveryBytePositionVariesAcrossCids) {
    std::vector<std::set<uint8_t>> per_position(kTokenLen);
    for (uint8_t i = 0; i < 64; ++i) {
        const auto token = TokenFor(Cid(i));
        for (uint32_t j = 0; j < kTokenLen; ++j) {
            per_position[j].insert(token[j]);
        }
    }
    for (uint32_t j = 0; j < kTokenLen; ++j) {
        EXPECT_GT(per_position[j].size(), 1u) << "byte " << j << " never changed across 64 CIDs";
    }
}

TEST(StatelessResetTokenTest, ManyCidsProduceDistinctTokens) {
    std::set<Token> seen;
    for (uint8_t i = 0; i < 255; ++i) {
        seen.insert(TokenFor(Cid(i)));
    }
    EXPECT_EQ(seen.size(), 255u);
}

// A zero-length CID is legal (RFC 9000 §5.1) and must still derive a usable,
// distinct token rather than being rejected or aliasing another CID.
TEST(StatelessResetTokenTest, ZeroLengthCidIsSupported) {
    uint8_t out[kTokenLen] = {0};
    ASSERT_TRUE(StatelessResetTokenGenerator::Instance().Generate(nullptr, 0, out));

    const Token empty_cid_token(out, out + kTokenLen);
    EXPECT_NE(empty_cid_token, Token(kTokenLen, 0));
    EXPECT_NE(empty_cid_token, TokenFor(Cid(0x00)));
}

TEST(StatelessResetTokenTest, VerifyAcceptsTheTokenItDerived) {
    const auto cid = Cid(0x55);
    const auto token = TokenFor(cid);

    EXPECT_TRUE(StatelessResetTokenGenerator::Instance().Verify(cid.data(), static_cast<uint8_t>(cid.size()),
        token.data()));
}

TEST(StatelessResetTokenTest, VerifyRejectsAForeignOrTamperedToken) {
    const auto cid = Cid(0x66);
    const auto other = Cid(0x77);
    auto token = TokenFor(cid);

    // Right token, wrong CID.
    EXPECT_FALSE(StatelessResetTokenGenerator::Instance().Verify(other.data(), static_cast<uint8_t>(other.size()),
        token.data()));

    // Right CID, token altered in the last byte -- the case a short-circuiting
    // compare would be slowest to reject and therefore leak.
    token[kTokenLen - 1] ^= 0xFF;
    EXPECT_FALSE(StatelessResetTokenGenerator::Instance().Verify(cid.data(), static_cast<uint8_t>(cid.size()),
        token.data()));
}

TEST(StatelessResetTokenTest, RejectsNullBuffers) {
    const auto cid = Cid(0x88);
    EXPECT_FALSE(StatelessResetTokenGenerator::Instance().Generate(cid.data(), static_cast<uint8_t>(cid.size()),
        nullptr));
    EXPECT_FALSE(StatelessResetTokenGenerator::Instance().Verify(cid.data(), static_cast<uint8_t>(cid.size()),
        nullptr));
}

// The key is loaded once, on first use, so this asserts against the environment
// as the process actually saw it rather than trying to mutate it mid-run. Under
// the default test environment the variable is unset, which means tokens do not
// survive a restart -- correct for tests, a misconfiguration in production.
TEST(StatelessResetTokenTest, KeyPersistenceReflectsTheEnvironment) {
    const char* env_key = std::getenv("QUICX_STATELESS_RESET_KEY");
    const bool env_supplies_usable_key =
        env_key != nullptr && *env_key != '\0' && strlen(env_key) >= StatelessResetTokenGenerator::kMinKeyLength * 2;

    EXPECT_EQ(StatelessResetTokenGenerator::Instance().IsKeyPersistent(), env_supplies_usable_key);
}

}  // namespace
}  // namespace quic
}  // namespace quicx
