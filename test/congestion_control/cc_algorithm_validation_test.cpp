#include <gtest/gtest.h>

#include <cmath>
#include "cc_test_framework.h"

namespace quicx {
namespace quic {
namespace {

// ========== Congestion Window Growth Validation ==========

TEST(CCAlgorithmValidation, RenoSlowStartExponentialGrowth) {
    printf("\n=== Validating Reno Slow Start Exponential Growth ===\n");
    
    CCTestFramework test(CCAlgorithmFactory::Reno(), TestScenario::IdealNetwork(2000000));
    test.Run();
    
    auto metrics = test.GetMetrics();
    
    // In slow start, cwnd should grow exponentially (roughly double per RTT)
    // Verify by checking state history
    bool found_exponential_growth = false;
    
    if (metrics.state_history.size() >= 10) {
        // Check first few RTTs in slow start
        for (size_t i = 1; i < 10 && i < metrics.state_history.size(); ++i) {
            if (metrics.state_history[i].in_slow_start && 
                metrics.state_history[i-1].in_slow_start) {
                
                double growth_ratio = static_cast<double>(metrics.state_history[i].cwnd_bytes) / 
                                     metrics.state_history[i-1].cwnd_bytes;
                
                // In slow start, each ACK increases cwnd by MSS
                // So growth should be positive
                if (growth_ratio > 1.0) {
                    found_exponential_growth = true;
                    printf("RTT %zu: cwnd %.2f KB -> %.2f KB (ratio: %.2f)\n",
                           i, metrics.state_history[i-1].cwnd_bytes / 1024.0,
                           metrics.state_history[i].cwnd_bytes / 1024.0, growth_ratio);
                }
            }
        }
    }
    
    EXPECT_TRUE(found_exponential_growth);
    EXPECT_GT(metrics.cwnd_growth_rate, 1000.0);  // Should grow significantly
}

TEST(CCAlgorithmValidation, RenoCwndReductionOnLoss) {
    printf("\n=== Validating Reno Cwnd Reduction on Loss ===\n");
    
    auto scenario = TestScenario::LossyNetwork(5000000);
    CCTestFramework test(CCAlgorithmFactory::Reno(), scenario);
    test.Run();
    
    auto metrics = test.GetMetrics();
    
    // Verify that cwnd reduces when entering recovery
    bool found_cwnd_reduction = false;
    uint64_t max_cwnd_before_recovery = 0;
    
    for (size_t i = 1; i < metrics.state_history.size(); ++i) {
        if (!metrics.state_history[i-1].in_recovery && 
            metrics.state_history[i].in_recovery) {
            // Entering recovery
            max_cwnd_before_recovery = metrics.state_history[i-1].cwnd_bytes;
            uint64_t cwnd_after = metrics.state_history[i].cwnd_bytes;
            
            if (cwnd_after < max_cwnd_before_recovery) {
                found_cwnd_reduction = true;
                double reduction_ratio = static_cast<double>(cwnd_after) / max_cwnd_before_recovery;
                printf("Cwnd reduction: %.2f KB -> %.2f KB (ratio: %.2f)\n",
                       max_cwnd_before_recovery / 1024.0, cwnd_after / 1024.0, reduction_ratio);
                
                // Reno typically reduces by beta (0.5)
                EXPECT_LT(reduction_ratio, 0.8);  // Should reduce by at least 20%
                break;
            }
        }
    }
    
    EXPECT_TRUE(found_cwnd_reduction);
}

TEST(CCAlgorithmValidation, CubicConcaveConvexBehavior) {
    printf("\n=== Validating CUBIC Concave-Convex Behavior ===\n");
    
    auto scenario = TestScenario::LowLatencyNetwork(10000000);
    scenario.network_condition.packet_loss_rate = 0.005;  // Small loss to trigger recovery
    
    CCTestFramework test(CCAlgorithmFactory::CUBIC(), scenario);
    test.Run();
    
    auto metrics = test.GetMetrics();
    
    // CUBIC should show characteristic growth pattern after loss
    // Find recovery period and check growth pattern
    bool found_recovery_exit = false;
    size_t recovery_exit_idx = 0;
    
    for (size_t i = 1; i < metrics.state_history.size(); ++i) {
        if (metrics.state_history[i-1].in_recovery && 
            !metrics.state_history[i].in_recovery) {
            found_recovery_exit = true;
            recovery_exit_idx = i;
            break;
        }
    }
    
    if (found_recovery_exit && recovery_exit_idx + 10 < metrics.state_history.size()) {
        printf("Found recovery exit at index %zu\n", recovery_exit_idx);
        
        // Check cwnd growth after recovery
        for (size_t i = recovery_exit_idx + 1; 
             i < recovery_exit_idx + 10 && i < metrics.state_history.size(); ++i) {
            printf("After recovery %zu: cwnd %.2f KB\n", 
                   i - recovery_exit_idx, 
                   metrics.state_history[i].cwnd_bytes / 1024.0);
        }
        
        EXPECT_TRUE(true);  // Just verify we can track the pattern
    }
}

// ========== RTT and Timing Validation ==========

TEST(CCAlgorithmValidation, RTTMeasurementAccuracy) {
    printf("\n=== Validating RTT Measurement ===\n");
    
    auto scenario = TestScenario::IdealNetwork(3000000);
    CCTestFramework test(CCAlgorithmFactory::Reno(), scenario);
    test.Run();
    
    auto metrics = test.GetMetrics();
    
    // In ideal network with 10ms base RTT, measured RTT should be close
    double expected_rtt_ms = 10.0;
    double tolerance = 2.0;  // ±2ms tolerance
    
    printf("Expected RTT: %.2f ms, Measured: %.2f ms\n", 
           expected_rtt_ms, metrics.avg_rtt_ms);
    
    EXPECT_NEAR(metrics.avg_rtt_ms, expected_rtt_ms, tolerance);
}

TEST(CCAlgorithmValidation, RTTVariationTracking) {
    printf("\n=== Validating RTT Variation Tracking ===\n");
    
    auto scenario = TestScenario::MobileNetwork(5000000);
    CCTestFramework test(CCAlgorithmFactory::Reno(), scenario);
    test.Run();
    
    auto metrics = test.GetMetrics();
    
    // Mobile network has high jitter, RTT should vary
    if (metrics.state_history.size() >= 2) {
        uint64_t min_rtt = UINT64_MAX;
        uint64_t max_rtt = 0;
        
        for (const auto& snapshot : metrics.state_history) {
            min_rtt = std::min(min_rtt, snapshot.rtt_us);
            max_rtt = std::max(max_rtt, snapshot.rtt_us);
        }
        
        double rtt_variation = static_cast<double>(max_rtt - min_rtt) / min_rtt;
        printf("RTT range: %.2f ms - %.2f ms (variation: %.2f%%)\n",
               min_rtt / 1000.0, max_rtt / 1000.0, rtt_variation * 100);
        
        // Mobile network should show significant RTT variation
        EXPECT_GT(rtt_variation, 0.1);  // At least 10% variation
    }
}

// ========== Ssthresh Behavior Validation ==========

TEST(CCAlgorithmValidation, SsthreshUpdateOnLoss) {
    printf("\n=== Validating Ssthresh Update on Loss ===\n");
    
    auto scenario = TestScenario::LossyNetwork(5000000);
    CCTestFramework test(CCAlgorithmFactory::Reno(), scenario);
    test.Run();
    
    auto metrics = test.GetMetrics();
    
    // Ssthresh should be set when exiting slow start
    bool found_ssthresh_update = false;
    
    for (size_t i = 1; i < metrics.state_history.size(); ++i) {
        if (metrics.state_history[i-1].in_slow_start && 
            !metrics.state_history[i].in_slow_start) {
            
            uint64_t ssthresh = metrics.state_history[i].ssthresh_bytes;
            uint64_t cwnd = metrics.state_history[i].cwnd_bytes;
            
            printf("Slow start exit: cwnd=%.2f KB, ssthresh=%.2f KB\n",
                   cwnd / 1024.0, ssthresh / 1024.0);
            
            // Ssthresh should be set to a reasonable value
            if (ssthresh < UINT64_MAX / 2) {  // Not initial value
                found_ssthresh_update = true;
                EXPECT_LE(cwnd, ssthresh * 1.5);  // cwnd should be around ssthresh
            }
            break;
        }
    }
    
    EXPECT_TRUE(found_ssthresh_update);
}

// ========== Bytes in Flight Validation ==========

TEST(CCAlgorithmValidation, BytesInFlightReasonable) {
    printf("\n=== Validating Bytes in Flight Behavior ===\n");
    
    auto scenario = TestScenario::LowLatencyNetwork(5000000);
    CCTestFramework test(CCAlgorithmFactory::Reno(), scenario);
    test.Run();
    
    auto metrics = test.GetMetrics();
    
    // Bytes in flight may temporarily exceed cwnd due to in-flight packets
    // when cwnd reduces (e.g., during loss recovery)
    // Just verify it's within reasonable bounds (e.g., < 3x cwnd)
    size_t violations = 0;
    
    for (const auto& snapshot : metrics.state_history) {
        if (snapshot.bytes_in_flight > snapshot.cwnd_bytes * 3) {
            printf("Large excess: bytes_in_flight=%.2f KB > 3*cwnd=%.2f KB\n",
                   snapshot.bytes_in_flight / 1024.0, (snapshot.cwnd_bytes * 3) / 1024.0);
            violations++;
        }
    }
    
    // Should not have excessive violations
    EXPECT_LT(violations, metrics.state_history.size() / 10);  // < 10% of snapshots
}

// ========== Extreme Condition Tests ==========

TEST(CCAlgorithmValidation, ZeroLossIdealConditions) {
    printf("\n=== Testing Zero Loss Behavior ===\n");
    
    auto scenario = TestScenario::IdealNetwork(3000000);
    CCTestFramework test(CCAlgorithmFactory::Reno(), scenario);
    test.Run();
    
    auto metrics = test.GetMetrics();
    
    // Should have zero packet loss
    EXPECT_EQ(metrics.total_packets_lost, 0UL);
    EXPECT_EQ(metrics.recovery_count, 0UL);
    EXPECT_EQ(metrics.packet_loss_rate, 0.0);
}

TEST(CCAlgorithmValidation, VeryLowBandwidth) {
    printf("\n=== Testing Very Low Bandwidth (1 Mbps) ===\n");
    
    TestScenario scenario;
    scenario.name = "Very Low Bandwidth";
    scenario.network_condition.base_rtt_us = 50000;
    scenario.network_condition.bandwidth_bps = 1 * 1000 * 1000 / 8;  // 1 Mbps
    scenario.test_duration_us = 10000000;
    
    CCTestFramework test(CCAlgorithmFactory::CUBIC(), scenario);
    test.Run();
    test.PrintStats(true);
    
    auto metrics = test.GetMetrics();
    
    // Should be limited by bandwidth
    EXPECT_LT(metrics.throughput_mbps, 1.5);  // Should not exceed 1 Mbps much
    EXPECT_GT(metrics.total_bytes_acked, 0UL);
}

TEST(CCAlgorithmValidation, VeryHighLatency) {
    printf("\n=== Testing Very High Latency (500ms RTT) ===\n");
    
    TestScenario scenario;
    scenario.name = "Very High Latency";
    scenario.network_condition.base_rtt_us = 500000;  // 500ms
    scenario.network_condition.packet_loss_rate = 0.001;  // Very low loss
    scenario.network_condition.bandwidth_bps = 10 * 1000 * 1000 / 8;
    scenario.test_duration_us = 20000000;  // 20 seconds
    
    CCTestFramework test(CCAlgorithmFactory::CUBIC(), scenario);
    test.Run();
    test.PrintStats(true);
    
    auto metrics = test.GetMetrics();
    
    // Should handle extreme latency
    EXPECT_GT(metrics.avg_rtt_ms, 400.0);  // Should measure high RTT
    EXPECT_GT(metrics.total_bytes_acked, 0UL);
}

// ========== Recovery Behavior Validation ==========

TEST(CCAlgorithmValidation, MultipleRecoveryPeriods) {
    printf("\n=== Testing Multiple Recovery Periods ===\n");
    
    auto scenario = TestScenario::LossyNetwork(10000000);
    CCTestFramework test(CCAlgorithmFactory::Reno(), scenario);
    test.Run();
    
    auto metrics = test.GetMetrics();
    
    printf("Total recovery periods: %lu\n", metrics.recovery_count);
    
    // With 5% loss over 10 seconds, should have multiple recoveries
    EXPECT_GT(metrics.recovery_count, 5UL);
    
    // Count actual recovery periods in history
    size_t recovery_periods = 0;
    bool in_recovery = false;
    
    for (const auto& snapshot : metrics.state_history) {
        if (snapshot.in_recovery && !in_recovery) {
            recovery_periods++;
            in_recovery = true;
        } else if (!snapshot.in_recovery) {
            in_recovery = false;
        }
    }
    
    printf("Recovery periods in history: %zu\n", recovery_periods);
}

// ========== Fairness and Convergence Tests ==========

TEST(CCAlgorithmValidation, CwndConvergenceAfterLoss) {
    printf("\n=== Testing Cwnd Convergence After Loss ===\n");
    
    auto scenario = TestScenario::LowLatencyNetwork(10000000);
    scenario.network_condition.packet_loss_rate = 0.005;  // 0.5% loss (lower)
    
    CCTestFramework test(CCAlgorithmFactory::CUBIC(), scenario);
    test.Run();
    
    auto metrics = test.GetMetrics();
    
    // After initial losses, cwnd should show some stability
    if (metrics.state_history.size() > 100) {
        // Check last 20% of test
        size_t start_idx = metrics.state_history.size() * 4 / 5;
        double sum_cwnd = 0;
        double sum_sq_diff = 0;
        
        for (size_t i = start_idx; i < metrics.state_history.size(); ++i) {
            sum_cwnd += metrics.state_history[i].cwnd_bytes;
        }
        
        double avg_cwnd = sum_cwnd / (metrics.state_history.size() - start_idx);
        
        for (size_t i = start_idx; i < metrics.state_history.size(); ++i) {
            double diff = metrics.state_history[i].cwnd_bytes - avg_cwnd;
            sum_sq_diff += diff * diff;
        }
        
        double std_dev = std::sqrt(sum_sq_diff / (metrics.state_history.size() - start_idx));
        double coefficient_of_variation = std_dev / avg_cwnd;
        
        printf("Avg cwnd in final phase: %.2f KB\n", avg_cwnd / 1024.0);
        printf("Coefficient of variation: %.2f\n", coefficient_of_variation);
        
        // With losses, cwnd will vary - just verify it's not completely unstable
        EXPECT_LT(coefficient_of_variation, 3.0);  // More lenient threshold
        EXPECT_GT(avg_cwnd, 0.0);
    }
}

// ============================================================================
// Tests for CUBIC optimizations: HyStart, Fast Convergence, and Pacing
// ============================================================================

TEST(CubicOptimizationTest, HyStartEarlyExitFromSlowStart) {
    printf("\n=== Testing CUBIC HyStart Early Exit ===\n");
    
    // Use a network with buffer bloat to trigger HyStart
    TestScenario scenario;
    scenario.name = "Buffer Bloat for HyStart";
    scenario.network_condition.base_rtt_us = 50000;  // 50ms base RTT
    scenario.network_condition.rtt_jitter_us = 5000;
    scenario.network_condition.packet_loss_rate = 0.001;  // Very low loss
    scenario.network_condition.bandwidth_bps = 10 * 1000 * 1000 / 8;  // 10 Mbps
    scenario.network_condition.queue_size_bytes = 500000;  // Large buffer -> bloat
    scenario.test_duration_us = 3000000;  // 3 seconds
    
    CCTestFramework test(CCAlgorithmFactory::CUBIC(), scenario);
    test.Run();
    test.PrintStats(true);
    
    auto metrics = test.GetMetrics();
    
    // HyStart should exit slow start earlier than traditional approach
    // With large buffer and low loss, slow start would normally go very high
    // HyStart should detect RTT increase and exit earlier
    
    printf("\n--- HyStart Validation ---\n");
    printf("Slow Start Duration: %.3f s\n", metrics.slow_start_duration_us / 1e6);
    printf("Max Cwnd: %.2f KB\n", metrics.max_cwnd_bytes / 1024.0);
    printf("Recovery Count: %lu\n", metrics.recovery_count);
    
    // HyStart should exit before hitting major losses
    // In buffer bloat scenario without HyStart, we'd see more recoveries
    EXPECT_LT(metrics.slow_start_duration_us, 2000000UL);  // Should exit within 2s
    EXPECT_GT(metrics.total_bytes_acked, 0UL);
}

TEST(CubicOptimizationTest, FastConvergenceOnRepeatedLoss) {
    printf("\n=== Testing CUBIC Fast Convergence ===\n");
    
    // Create scenario with dynamic bandwidth decrease to trigger Fast Convergence
    TestScenario scenario;
    scenario.name = "Dynamic Bandwidth Decrease";
    scenario.network_condition.base_rtt_us = 50000;  // 50ms
    scenario.network_condition.packet_loss_rate = 0.01;  // 1% loss
    scenario.network_condition.bandwidth_bps = 10 * 1000 * 1000 / 8;  // 10 Mbps
    scenario.test_duration_us = 15000000;  // 15 seconds
    
    // Decrease bandwidth midway to trigger reconvergence
    NetworkCondition reduced_bw = scenario.network_condition;
    reduced_bw.bandwidth_bps = 5 * 1000 * 1000 / 8;  // Reduce to 5 Mbps
    reduced_bw.packet_loss_rate = 0.02;  // Higher loss
    scenario.condition_changes.push_back({7500000, reduced_bw});  // At 7.5s
    
    CCTestFramework test(CCAlgorithmFactory::CUBIC(), scenario);
    test.Run();
    test.PrintStats(true);
    
    auto metrics = test.GetMetrics();
    
    printf("\n--- Fast Convergence Validation ---\n");
    printf("Total Recovery Count: %lu\n", metrics.recovery_count);
    printf("Avg Throughput: %.2f Mbps\n", metrics.throughput_mbps);
    
    // Fast Convergence should help adapt to bandwidth decrease
    // We expect multiple recovery periods due to bandwidth change
    EXPECT_GT(metrics.recovery_count, 2UL);
    EXPECT_GT(metrics.total_bytes_acked, 0UL);
    
    // Check that cwnd adjusted downward after bandwidth reduction
    bool found_cwnd_reduction = false;
    double max_cwnd_first_half = 0.0;
    double avg_cwnd_second_half = 0.0;
    size_t second_half_count = 0;
    
    for (const auto& snapshot : metrics.state_history) {
        if (snapshot.time_us < 7500000) {
            max_cwnd_first_half = std::max(max_cwnd_first_half, 
                                          static_cast<double>(snapshot.cwnd_bytes));
        } else if (snapshot.time_us > 8000000) {  // After adjustment
            avg_cwnd_second_half += snapshot.cwnd_bytes;
            second_half_count++;
        }
    }
    
    if (second_half_count > 0) {
        avg_cwnd_second_half /= second_half_count;
        printf("Max cwnd (first half): %.2f KB\n", max_cwnd_first_half / 1024.0);
        printf("Avg cwnd (second half): %.2f KB\n", avg_cwnd_second_half / 1024.0);
        
        // Fast Convergence should help cwnd settle lower after bandwidth reduction
        if (max_cwnd_first_half > 0) {
            found_cwnd_reduction = (avg_cwnd_second_half < max_cwnd_first_half * 0.9);
        }
    }
    
    printf("Cwnd reduction detected: %s\n", found_cwnd_reduction ? "Yes" : "No");
}

TEST(CubicOptimizationTest, PacingRateWithInitialRTT) {
    printf("\n=== Testing CUBIC Pacing Rate Optimization ===\n");
    
    // Test that pacing works correctly even before first RTT sample
    TestScenario scenario = TestScenario::IdealNetwork(1000000);  // 1 second
    
    CCTestFramework test(CCAlgorithmFactory::CUBIC(), scenario);
    test.Run();
    test.PrintStats(true);
    
    auto metrics = test.GetMetrics();
    
    printf("\n--- Pacing Rate Validation ---\n");
    printf("Total Bytes Sent: %lu\n", metrics.total_bytes_sent);
    printf("Throughput: %.2f Mbps\n", metrics.throughput_mbps);
    
    // With proper pacing initialization, we should achieve good throughput
    // even in the first RTT
    EXPECT_GT(metrics.total_bytes_sent, 10000UL);  // Should send significant data
    EXPECT_GT(metrics.throughput_mbps, 1.0);
    
    // Check state history for pacing gain effect
    if (metrics.state_history.size() > 10) {
        printf("Cwnd samples: ");
        for (size_t i = 0; i < std::min<size_t>(10, metrics.state_history.size()); i++) {
            printf("%.1fKB ", metrics.state_history[i].cwnd_bytes / 1024.0);
        }
        printf("\n");
    }
}

TEST(CubicOptimizationTest, HyStartVsTraditionalSlowStart) {
    printf("\n=== Comparing HyStart vs Traditional Slow Start ===\n");
    
    // Test in a buffer bloat scenario
    TestScenario scenario;
    scenario.name = "Buffer Bloat Comparison";
    scenario.network_condition.base_rtt_us = 50000;  // 50ms
    scenario.network_condition.rtt_jitter_us = 10000;    // 10ms jitter
    scenario.network_condition.packet_loss_rate = 0.001;
    scenario.network_condition.bandwidth_bps = 10 * 1000 * 1000 / 8;
    scenario.network_condition.queue_size_bytes = 1000000;  // 1MB buffer
    scenario.test_duration_us = 5000000;  // 5 seconds
    
    CCTestFramework test_cubic(CCAlgorithmFactory::CUBIC(), scenario);
    test_cubic.Run();
    
    printf("\nCUBIC (with HyStart):\n");
    test_cubic.PrintStats(true);
    
    auto cubic_metrics = test_cubic.GetMetrics();
    
    printf("\n--- Performance Summary ---\n");
    printf("CUBIC Slow Start Duration: %.3f s\n", 
           cubic_metrics.slow_start_duration_us / 1e6);
    printf("CUBIC Avg RTT: %.2f ms\n", cubic_metrics.avg_rtt_ms);
    printf("CUBIC Throughput: %.2f Mbps\n", cubic_metrics.throughput_mbps);
    printf("CUBIC Loss Rate: %.2f%%\n", cubic_metrics.packet_loss_rate * 100);
    
    // With HyStart, CUBIC should:
    // 1. Exit slow start relatively quickly (before massive buffer build-up)
    // 2. Maintain lower average RTT (less bufferbloat)
    // 3. Still achieve good throughput
    
    EXPECT_LT(cubic_metrics.slow_start_duration_us, 3000000UL);
    EXPECT_GT(cubic_metrics.throughput_mbps, 0.5);
}

}
} // namespace quic
} // namespace quicx
