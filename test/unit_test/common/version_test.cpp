// Copyright (c) the QuicX authors.
// SPDX-License-Identifier: BSD-3-Clause
//
// Sanity tests for the QuicX product version macros and helpers
// declared in <common/version.h>.  These are NOT tests of the
// on-the-wire QUIC protocol version (RFC 9000 / RFC 9369), which lives
// in <quic/common/version.h> and is exercised by other tests.

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include <quicx/common/version.h>

namespace {

TEST(QuicxVersionTest, MacrosAreNonNegativeAndConsistent) {
    // Sanity: macros are integers we can compare.
    EXPECT_GE(QUICX_VERSION_MAJOR, 0);
    EXPECT_GE(QUICX_VERSION_MINOR, 0);
    EXPECT_GE(QUICX_VERSION_PATCH, 0);

    // QUICX_VERSION_NUMBER must encode and round-trip the components.
    const unsigned int encoded = QUICX_VERSION_NUMBER(
        QUICX_VERSION_MAJOR, QUICX_VERSION_MINOR, QUICX_VERSION_PATCH);
    EXPECT_EQ(encoded, QUICX_VERSION);
}

TEST(QuicxVersionTest, StringMatchesNumericComponents) {
    // Construct "<major>.<minor>.<patch>" and compare against the macro
    // expansion to guarantee they agree.
    std::string expected = std::to_string(QUICX_VERSION_MAJOR) + "." +
                           std::to_string(QUICX_VERSION_MINOR) + "." +
                           std::to_string(QUICX_VERSION_PATCH);
    EXPECT_STREQ(QUICX_VERSION_STRING, expected.c_str());
}

TEST(QuicxVersionTest, GetVersionStringMatchesMacro) {
    const char* s = ::quicx::GetVersionString();
    ASSERT_NE(s, nullptr);
    EXPECT_STREQ(s, QUICX_VERSION_STRING);
}

TEST(QuicxVersionTest, GetVersionNumberMatchesMacro) {
    EXPECT_EQ(::quicx::GetVersionNumber(), QUICX_VERSION);
}

TEST(QuicxVersionTest, NumberMonotonicallyIncreases) {
    // Spot-check the monotonicity of the encoded number so downstream
    // code can rely on `>=` comparisons.
    EXPECT_GT(QUICX_VERSION_NUMBER(0, 1, 1), QUICX_VERSION_NUMBER(0, 1, 0));
    EXPECT_GT(QUICX_VERSION_NUMBER(0, 2, 0), QUICX_VERSION_NUMBER(0, 1, 99));
    EXPECT_GT(QUICX_VERSION_NUMBER(1, 0, 0), QUICX_VERSION_NUMBER(0, 99, 99));
}

}  // namespace
