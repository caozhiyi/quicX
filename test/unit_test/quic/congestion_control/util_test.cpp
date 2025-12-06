#include <cassert>
#include "quic/congestion_control/util.h"

namespace quicx{
namespace quic{
namespace congestion_control {
namespace {

void test_basic_operations() {
    // Test basic multiplication
    uint64_t result;
    assert(!multiply_overflow(2, 3, result) && result == 6);
    assert(!multiply_overflow(0, 100, result) && result == 0);
    assert(!multiply_overflow(100, 0, result) && result == 0);
    
    // Test overflow detection
    assert(multiply_overflow(UINT64_MAX, 2, result));
    assert(multiply_overflow(2, UINT64_MAX, result));
    
    // Test saturation
    assert(multiply_saturate(UINT64_MAX, 2) == UINT64_MAX);
    assert(multiply_saturate(2, 3) == 6);
    
    // Test division
    assert(divide_safe(6, 2) == 3);
    assert(divide_safe(0, 5) == 0);
    assert(divide_safe(5, 0) == UINT64_MAX);
}
    
void test_muldiv_safe() {
    // Test basic cases
    assert(muldiv_safe(10, 5, 2) == 25);
    assert(muldiv_safe(10, 5, 3) == 16); // 50/3 = 16.66... -> 16
    assert(muldiv_safe(0, 100, 10) == 0);
    assert(muldiv_safe(100, 0, 10) == 0);
    assert(muldiv_safe(100, 10, 0) == 0);
    
    // Test large numbers (similar to BBR usage)
    uint64_t large_num = 1000000000; // 1 billion
    uint64_t result = muldiv_safe(large_num, 8 * 1000000, 1000000);
    assert(result == large_num * 8);
    
    // Test overflow cases
    uint64_t huge_num = UINT64_MAX / 2;
    uint64_t overflow_result = muldiv_safe(huge_num, 3, 1);
    assert(overflow_result == UINT64_MAX); // Should saturate
}
    
void test_bbr_scenarios() {
    // Simulate typical BBR calculations
    uint64_t bytes_acked = 1500; // Typical packet size
    uint64_t srtt_us = 10000;    // 10ms RTT
    uint64_t sample_bps = muldiv_safe(bytes_acked, 8 * 1000000, srtt_us);
    assert(sample_bps == 1200000000); // 1.2 Gbps
    
    // Test BDP calculation
    uint64_t bw_bps = 1000000000; // 1 Gbps
    uint64_t min_rtt_us = 5000;   // 5ms
    uint64_t bdp = muldiv_safe(bw_bps, min_rtt_us, 1000000);
    assert(bdp == 5000); // 5KB BDP
    
    // Test with gain
    uint64_t bdp_with_gain = muldiv_safe(bdp, 2000, 1000); // 2.0 gain
    assert(bdp_with_gain == 10000); // 10KB
}

}    
}
}
}
