#include <gtest/gtest.h>

#include "quic/congestion_control/util.h"

namespace quicx {
namespace quic {
namespace congestion_control {
namespace {

TEST(UtilTest, BasicMultiplyOperations) {
    // Test basic multiplication
    uint64_t result;
    ASSERT_FALSE(multiply_overflow(2, 3, result));
    EXPECT_EQ(result, 6u);
    ASSERT_FALSE(multiply_overflow(0, 100, result));
    EXPECT_EQ(result, 0u);
    ASSERT_FALSE(multiply_overflow(100, 0, result));
    EXPECT_EQ(result, 0u);

    // Test overflow detection
    EXPECT_TRUE(multiply_overflow(UINT64_MAX, 2, result));
    EXPECT_TRUE(multiply_overflow(2, UINT64_MAX, result));

    // Test saturation
    EXPECT_EQ(multiply_saturate(UINT64_MAX, 2), UINT64_MAX);
    EXPECT_EQ(multiply_saturate(2, 3), 6u);

    // Test division
    EXPECT_EQ(divide_safe(6, 2), 3u);
    EXPECT_EQ(divide_safe(0, 5), 0u);
    EXPECT_EQ(divide_safe(5, 0), UINT64_MAX);
}

TEST(UtilTest, MuldivSafe) {
    // Test basic cases
    EXPECT_EQ(muldiv_safe(10, 5, 2), 25u);
    EXPECT_EQ(muldiv_safe(10, 5, 3), 16u);  // 50/3 = 16.66... -> 16
    EXPECT_EQ(muldiv_safe(0, 100, 10), 0u);
    EXPECT_EQ(muldiv_safe(100, 0, 10), 0u);
    EXPECT_EQ(muldiv_safe(100, 10, 0), 0u);

    // Test large numbers (similar to BBR usage)
    uint64_t large_num = 1000000000;  // 1 billion
    uint64_t result = muldiv_safe(large_num, 8 * 1000000, 1000000);
    EXPECT_EQ(result, large_num * 8);

    // Test overflow cases
    uint64_t huge_num = UINT64_MAX / 2;
    uint64_t overflow_result = muldiv_safe(huge_num, 3, 1);
    EXPECT_EQ(overflow_result, UINT64_MAX);  // Should saturate
}

TEST(UtilTest, BbrScenarios) {
    // Simulate typical BBR calculations. Note the units: these expectations
    // follow muldiv_safe()'s actual semantics — the original dead helper
    // (never executed) asserted 1.2 Gbps / 5 KB / 10 KB, mis-converting
    // us→s and bits→bytes.
    uint64_t bytes_acked = 1500;  // Typical packet size
    uint64_t srtt_us = 10000;     // 10ms RTT
    uint64_t sample_bps = muldiv_safe(bytes_acked, 8 * 1000000, srtt_us);
    EXPECT_EQ(sample_bps, 1200000u);  // 1500 B / 10 ms = 1.2 MB/s

    // Test BDP calculation
    uint64_t bw_bps = 1000000000;  // 1e9 B/s
    uint64_t min_rtt_us = 5000;    // 5ms
    uint64_t bdp = muldiv_safe(bw_bps, min_rtt_us, 1000000);
    EXPECT_EQ(bdp, 5000000u);  // 1e9 B/s * 5 ms = 5 MB

    // Test with gain
    uint64_t bdp_with_gain = muldiv_safe(bdp, 2000, 1000);  // 2.0 gain
    EXPECT_EQ(bdp_with_gain, 10000000u);                    // 10 MB
}

}  // namespace
}  // namespace congestion_control
}  // namespace quic
}  // namespace quicx
