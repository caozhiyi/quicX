// Tests for ParsePreferredAddress (now a free function in quic/connection/util.h).
//
// The preferred_address transport parameter is a peer-supplied string
// (TransportParam::Merge overwrites our copy with theirs, and the decoder only
// bounds-checks the bytes -- it does not validate content). The previous parse
// was `std::stoi(pref.substr(pref.find(':') + 1))` with no guard, which throws
// on input the peer fully controls; nothing in the connection path catches, so
// the process aborted. Verified against the real strings below:
//
//     "::1:4433"                -> stoi: no conversion   (a LEGITIMATE IPv6value)
//     "a:b"                         -> stoi: no conversion
//     "host:99999999999999999999"   -> stoi: out of range
//     "x:"                          -> stoi: no conversion
//
// These tests pin that every one of those is now rejected rather than fatal.

#include <gtest/gtest.h>

#include "common/network/address.h"

#include "quic/connection/util.h"

namespace quicx {
namespace quic {
namespace {

// ===== Inputs that used to abort the process =====

TEST(PreferredAddressParseTest, RejectsBareIpv6InsteadOfCrashing) {
    common::Address out;
    // Ambiguous without brackets: is the last group a port or part of the
    //  address? Reject rather than guess. This used to throw.
    EXPECT_FALSE(ParsePreferredAddress("::1:4433", out));
}

TEST(PreferredAddressParseTest, RejectsNonNumericPort) {
    common::Address out;
    EXPECT_FALSE(ParsePreferredAddress("a:b", out));
}

TEST(PreferredAddressParseTest, RejectsPortOverflowingUint64) {
    common::Address out;
    EXPECT_FALSE(ParsePreferredAddress("host:99999999999999999999", out));
}

TEST(PreferredAddressParseTest, RejectsEmptyPort) {
    common::Address out;
    EXPECT_FALSE(ParsePreferredAddress("x:", out));
}

// ===== Other malformed input =====

TEST(PreferredAddressParseTest, RejectsMissingColon) {
    common::Address out;
    EXPECT_FALSE(ParsePreferredAddress("127.0.0.1", out));
}

TEST(PreferredAddressParseTest, RejectsEmptyHost) {
    common::Address out;
    EXPECT_FALSE(ParsePreferredAddress(":4433", out));
}

TEST(PreferredAddressParseTest, RejectsEmptyString) {
    common::Address out;
    EXPECT_FALSE(ParsePreferredAddress("", out));
}

// Ports must fit a uint16_t. The old code cast a parsed int straight to
//  uint16_t, so65536 silently became 0 and 65537 became 1.
TEST(PreferredAddressParseTest, RejectsPortAboveUint16RangeInsteadOfTruncating) {
    common::Address out;
    EXPECT_FALSE(ParsePreferredAddress("127.0.0.1:65536", out));
    EXPECT_FALSE(ParsePreferredAddress("127.0.0.1:70000", out));
}

TEST(PreferredAddressParseTest, RejectsZeroPort) {
    common::Address out;
    EXPECT_FALSE(ParsePreferredAddress("127.0.0.1:0", out));
}

TEST(PreferredAddressParseTest, RejectsNegativePort) {
    common::Address out;
    EXPECT_FALSE(ParsePreferredAddress("127.0.0.1:-1", out));
}

TEST(PreferredAddressParseTest, RejectsTrailingGarbageAfterPort) {
    common::Address out;
    EXPECT_FALSE(ParsePreferredAddress("127.0.0.1:443x", out));
    EXPECT_FALSE(ParsePreferredAddress("127.0.0.1:44 3", out));
}

TEST(PreferredAddressParseTest, RejectsUnterminatedBracket) {
    common::Address out;
    EXPECT_FALSE(ParsePreferredAddress("[::1:4433", out));
    EXPECT_FALSE(ParsePreferredAddress("[::1]4433", out));
    EXPECT_FALSE(ParsePreferredAddress("[]:4433", out));
}

// ===== Valid input =====

TEST(PreferredAddressParseTest, AcceptsIpv4) {
    common::Address out;
    ASSERT_TRUE(ParsePreferredAddress("127.0.0.1:4433", out));
    EXPECT_EQ(out.GetIp(), "127.0.0.1");
    EXPECT_EQ(out.GetPort(), 4433);
}

// Bracketed form is the unambiguous way to carry IPv6, and it is what the old
// parse mangled worst.
TEST(PreferredAddressParseTest, AcceptsBracketedIpv6) {
    common::Address out;
    ASSERT_TRUE(ParsePreferredAddress("[::1]:4433", out));
    EXPECT_EQ(out.GetIp(), "::1");
    EXPECT_EQ(out.GetPort(), 4433);
}

TEST(PreferredAddressParseTest, AcceptsFullBracketedIpv6) {
    common::Address out;
    ASSERT_TRUE(ParsePreferredAddress("[2001:db8::1]:443", out));
    EXPECT_EQ(out.GetIp(), "2001:db8::1");
    EXPECT_EQ(out.GetPort(), 443);
}

TEST(PreferredAddressParseTest, AcceptsBoundaryPorts) {
    common::Address out;
    ASSERT_TRUE(ParsePreferredAddress("10.0.0.1:1", out));
    EXPECT_EQ(out.GetPort(), 1);

    ASSERT_TRUE(ParsePreferredAddress("10.0.0.1:65535", out));
    EXPECT_EQ(out.GetPort(), 65535);
}

}  // namespace
}  // namespace quic
}  // namespace quicx
