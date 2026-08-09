// Tests for stateless reset token generation.
//
// RFC 9000 §10.3 requires the token to be "hard to guess". It was generated
// with `rand() % 256` in a loop, which fails that on three counts: rand() is a
// non-cryptographic LCG whose entire future output follows from its current
// state, nothing in this project ever calls srand() so the sequence is the same
// on every run, and rand() is not thread-safe while workers generate tokens
// concurrently.
//
// A peer that can guess a token can inject a Stateless Reset packet and tear
// down the connection at will (RFC 9000 §10.3).
//
// The project's own src/common/util/random.h names "stateless reset tokens" as
// a case that must use a CSPRNG, and points at ConnectionIDGenerator and
// RetryTokenManager as the examples to follow -- the one place that ignored it
// was this token.

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <set>
#include <vector>

#include "quic/connection/connection_id_coordinator.h"

namespace quicx {
namespace quic {
namespace {

constexpr uint32_t kTokenLen = 16;

std::vector<uint8_t> Generate() {
    std::vector<uint8_t> token(kTokenLen, 0);
    EXPECT_TRUE(ConnectionIDCoordinator::GenerateStatelessResetToken(token.data(), kTokenLen));
    return token;
}

// The decisive test: rand() is fully determined by its seed, so re-seeding and
// generating again reproduces the sequence. A CSPRNG does not care about srand.
TEST(StatelessResetTokenTest, NotReproducibleAfterReseeding) {
    std::srand(1);
    const auto first = Generate();

    std::srand(1);
    const auto second = Generate();

    EXPECT_NE(first, second) << "token sequence is reproducible from an srand() seed, "
                                "so it is coming from a non-cryptographic PRNG";
}

TEST(StatelessResetTokenTest, SuccessiveTokensDiffer) {
    const auto a = Generate();
    const auto b = Generate();
    EXPECT_NE(a, b);
}

TEST(StatelessResetTokenTest, FillsWholeTokenNotLeavingZeros) {
    const auto token = Generate();
    ASSERT_EQ(token.size(), kTokenLen);

    const std::vector<uint8_t> all_zero(kTokenLen, 0);
    EXPECT_NE(token, all_zero);
}

// Weak generators repeat quickly. 256 draws of a 16-byte token must all be
// distinct; a collision here means far too little entropy.
TEST(StatelessResetTokenTest, ManyTokensAreAllDistinct) {
    std::set<std::vector<uint8_t>> seen;
    for (int i = 0; i < 256; ++i) {
        seen.insert(Generate());
    }
    EXPECT_EQ(seen.size(), 256u);
}

// Every byte position must vary across draws. `rand() % 256` on some libc
// implementations has poor low-bit behaviour, and a partially-filled buffer
// would leave constant positions.
TEST(StatelessResetTokenTest, EveryBytePositionVaries) {
    std::vector<std::set<uint8_t>> per_position(kTokenLen);
    for (int i = 0; i < 64; ++i) {
        const auto token = Generate();
        for (uint32_t j = 0; j < kTokenLen; ++j) {
            per_position[j].insert(token[j]);
        }
    }
    for (uint32_t j = 0; j < kTokenLen; ++j) {
        EXPECT_GT(per_position[j].size(), 1u) << "byte " << j << " never changed across 64 tokens";
    }
}

TEST(StatelessResetTokenTest, RejectsNullOrZeroLength) {
    uint8_t buf[kTokenLen] = {0};
    EXPECT_FALSE(ConnectionIDCoordinator::GenerateStatelessResetToken(nullptr, kTokenLen));
    EXPECT_FALSE(ConnectionIDCoordinator::GenerateStatelessResetToken(buf, 0));
}

}  // namespace
}  // namespace quic
}  // namespace quicx
