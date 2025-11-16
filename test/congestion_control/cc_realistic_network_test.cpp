#include <gtest/gtest.h>

#include "cc_test_framework.h"

namespace quicx {
namespace quic {
namespace {

// ========== Realistic Network Scenario Tests ==========

TEST(RealisticNetworkTest, EnterpriseNetwork_BBRv2) {
    printf("\n=== Testing BBRv2 in Enterprise Network (1Gbps, 5ms RTT) ===\n");
    
    TestScenario scenario;
    scenario.name = "Enterprise Network";
    scenario.network_condition = NetworkCondition::EnterpriseNetwork();
    scenario.test_duration_us = 10000000;  // 10 seconds
    
    CCTestFramework test(CCAlgorithmFactory::BBRv2(), scenario);
    test.Run();
    test.PrintStats(true);
    
    auto metrics = test.GetMetrics();
    
    printf("\n--- Enterprise Network Performance ---\n");
    printf("Throughput: %.2f Mbps\n", metrics.throughput_mbps);
    printf("Max Cwnd: %.2f KB\n", metrics.max_cwnd_bytes / 1024.0);
    printf("Avg RTT: %.2f ms\n", metrics.avg_rtt_ms);
    printf("Expected BDP: ~%.2f KB (1Gbps × 5ms)\n", (1000.0 / 8.0 * 0.005 * 1000));
    
    // High throughput expected
    EXPECT_GT(metrics.throughput_mbps, 100.0);  // Should achieve >100Mbps
    EXPECT_GT(metrics.max_cwnd_bytes, 50 * 1460);  // Cwnd should grow significantly
}

TEST(RealisticNetworkTest, Broadband_CUBIC) {
    printf("\n=== Testing CUBIC in Broadband (100Mbps, 15ms RTT) ===\n");
    
    TestScenario scenario;
    scenario.name = "Broadband";
    scenario.network_condition = NetworkCondition::Broadband();
    scenario.test_duration_us = 10000000;  // 10 seconds
    
    CCTestFramework test(CCAlgorithmFactory::CUBIC(), scenario);
    test.Run();
    test.PrintStats(true);
    
    auto metrics = test.GetMetrics();
    
    printf("\n--- Broadband Performance ---\n");
    printf("Throughput: %.2f Mbps\n", metrics.throughput_mbps);
    printf("Max Cwnd: %.2f KB\n", metrics.max_cwnd_bytes / 1024.0);
    printf("Expected BDP: ~%.2f KB (100Mbps × 15ms)\n", (100.0 / 8.0 * 0.015 * 1000));
    
    EXPECT_GT(metrics.throughput_mbps, 10.0);
    EXPECT_GT(metrics.max_cwnd_bytes, 20 * 1460);
}

TEST(RealisticNetworkTest, WiFi_BBRv1) {
    printf("\n=== Testing BBRv1 in WiFi (300Mbps, 8ms RTT) ===\n");
    
    TestScenario scenario;
    scenario.name = "WiFi";
    scenario.network_condition = NetworkCondition::WiFi();
    scenario.test_duration_us = 10000000;  // 10 seconds
    
    CCTestFramework test(CCAlgorithmFactory::BBRv1(), scenario);
    test.Run();
    test.PrintStats(true);
    
    auto metrics = test.GetMetrics();
    
    printf("\n--- WiFi Performance ---\n");
    printf("Throughput: %.2f Mbps\n", metrics.throughput_mbps);
    printf("Max Cwnd: %.2f KB\n", metrics.max_cwnd_bytes / 1024.0);
    printf("Expected BDP: ~%.2f KB (300Mbps × 8ms)\n", (300.0 / 8.0 * 0.008 * 1000));
    
    EXPECT_GT(metrics.throughput_mbps, 30.0);
}

TEST(RealisticNetworkTest, FiveG_BBRv2) {
    printf("\n=== Testing BBRv2 in 5G Network (200Mbps, 20ms RTT) ===\n");
    
    TestScenario scenario;
    scenario.name = "5G Network";
    scenario.network_condition = NetworkCondition::FiveG();
    scenario.test_duration_us = 15000000;  // 15 seconds
    
    CCTestFramework test(CCAlgorithmFactory::BBRv2(), scenario);
    test.Run();
    test.PrintStats(true);
    
    auto metrics = test.GetMetrics();
    
    printf("\n--- 5G Performance ---\n");
    printf("Throughput: %.2f Mbps\n", metrics.throughput_mbps);
    printf("Max Cwnd: %.2f KB\n", metrics.max_cwnd_bytes / 1024.0);
    printf("Expected BDP: ~%.2f KB (200Mbps × 20ms)\n", (200.0 / 8.0 * 0.020 * 1000));
    printf("Loss Rate: %.2f%%\n", metrics.packet_loss_rate * 100);
    
    EXPECT_GT(metrics.throughput_mbps, 20.0);
    EXPECT_GT(metrics.max_cwnd_bytes, 30 * 1460);
}

TEST(RealisticNetworkTest, LTE_Reno) {
    printf("\n=== Testing Reno in LTE (50Mbps, 50ms RTT) ===\n");
    
    TestScenario scenario;
    scenario.name = "LTE Network";
    scenario.network_condition = NetworkCondition::LTE();
    scenario.test_duration_us = 15000000;  // 15 seconds
    
    CCTestFramework test(CCAlgorithmFactory::Reno(), scenario);
    test.Run();
    test.PrintStats(true);
    
    auto metrics = test.GetMetrics();
    
    printf("\n--- LTE Performance ---\n");
    printf("Throughput: %.2f Mbps\n", metrics.throughput_mbps);
    printf("Max Cwnd: %.2f KB\n", metrics.max_cwnd_bytes / 1024.0);
    printf("Recovery Count: %lu\n", metrics.recovery_count);
    
    // With Reno at 1% loss and 50ms RTT, theoretical throughput is often < 5 Mbps.
    // Use a slightly lower threshold that still indicates reasonable performance.
    EXPECT_GT(metrics.throughput_mbps, 4.5);
}

TEST(RealisticNetworkTest, CrossContinent_BBRv2_ProbeRTT) {
    printf("\n=== Testing BBRv2 ProbeRTT in Cross-Continent Link (100Mbps, 150ms RTT) ===\n");
    
    TestScenario scenario;
    scenario.name = "Cross-Continent";
    scenario.network_condition = NetworkCondition::CrossContinent();
    scenario.test_duration_us = 30000000;  // 30 seconds - enough for ProbeRTT
    
    CCTestFramework test(CCAlgorithmFactory::BBRv2(), scenario);
    test.Run();
    test.PrintStats(true);
    
    auto metrics = test.GetMetrics();
    
    printf("\n--- Cross-Continent Performance ---\n");
    printf("Throughput: %.2f Mbps\n", metrics.throughput_mbps);
    printf("Max Cwnd: %.2f KB\n", metrics.max_cwnd_bytes / 1024.0);
    printf("Expected BDP: ~%.2f KB (100Mbps × 150ms)\n", (100.0 / 8.0 * 0.150 * 1000));
    printf("Avg RTT: %.2f ms\n", metrics.avg_rtt_ms);
    
    // High latency + high bandwidth = very large BDP
    EXPECT_GT(metrics.throughput_mbps, 10.0);
    EXPECT_GT(metrics.max_cwnd_bytes, 100 * 1460);  // Should grow large for high BDP
}

TEST(RealisticNetworkTest, AlgorithmComparison_Broadband) {
    printf("\n=== Comparing Algorithms in Broadband Network ===\n");
    
    TestScenario scenario;
    scenario.name = "Broadband Comparison";
    scenario.network_condition = NetworkCondition::Broadband();
    scenario.test_duration_us = 10000000;  // 10 seconds
    
    CCTestFramework reno_test(CCAlgorithmFactory::Reno(), scenario);
    reno_test.Run();
    auto reno_metrics = reno_test.GetMetrics();
    
    CCTestFramework cubic_test(CCAlgorithmFactory::CUBIC(), scenario);
    cubic_test.Run();
    auto cubic_metrics = cubic_test.GetMetrics();
    
    CCTestFramework bbr_v1_test(CCAlgorithmFactory::BBRv1(), scenario);
    bbr_v1_test.Run();
    auto bbr_v1_metrics = bbr_v1_test.GetMetrics();
    
    CCTestFramework bbr_v2_test(CCAlgorithmFactory::BBRv2(), scenario);
    bbr_v2_test.Run();
    auto bbr_v2_metrics = bbr_v2_test.GetMetrics();
    
    printf("\n--- Algorithm Comparison ---\n");
    printf("%-10s | %10s | %10s | %10s | %10s\n", 
           "Algorithm", "Throughput", "Max Cwnd", "Loss Rate", "Avg RTT");
    printf("-----------|------------|------------|------------|------------\n");
    printf("%-10s | %7.2f Mbps | %7.2f KB | %8.2f%% | %7.2f ms\n",
           "Reno", reno_metrics.throughput_mbps, reno_metrics.max_cwnd_bytes / 1024.0,
           reno_metrics.packet_loss_rate * 100, reno_metrics.avg_rtt_ms);
    printf("%-10s | %7.2f Mbps | %7.2f KB | %8.2f%% | %7.2f ms\n",
           "CUBIC", cubic_metrics.throughput_mbps, cubic_metrics.max_cwnd_bytes / 1024.0,
           cubic_metrics.packet_loss_rate * 100, cubic_metrics.avg_rtt_ms);
    printf("%-10s | %7.2f Mbps | %7.2f KB | %8.2f%% | %7.2f ms\n",
           "BBRv1", bbr_v1_metrics.throughput_mbps, bbr_v1_metrics.max_cwnd_bytes / 1024.0,
           bbr_v1_metrics.packet_loss_rate * 100, bbr_v1_metrics.avg_rtt_ms);
    printf("%-10s | %7.2f Mbps | %7.2f KB | %8.2f%% | %7.2f ms\n",
           "BBRv2", bbr_v2_metrics.throughput_mbps, bbr_v2_metrics.max_cwnd_bytes / 1024.0,
           bbr_v2_metrics.packet_loss_rate * 100, bbr_v2_metrics.avg_rtt_ms);
    
    // All algorithms should achieve reasonable throughput
    EXPECT_GT(reno_metrics.throughput_mbps, 5.0);
    EXPECT_GT(cubic_metrics.throughput_mbps, 5.0);
    EXPECT_GT(bbr_v1_metrics.throughput_mbps, 5.0);
    EXPECT_GT(bbr_v2_metrics.throughput_mbps, 5.0);
}

TEST(RealisticNetworkTest, DynamicCondition_WiFi_to_LTE) {
    printf("\n=== Testing Dynamic Network Change: WiFi -> LTE ===\n");
    
    TestScenario scenario;
    scenario.name = "WiFi to LTE Handover";
    scenario.network_condition = NetworkCondition::WiFi();
    scenario.test_duration_us = 20000000;  // 20 seconds
    
    // Simulate handover from WiFi to LTE at 10 seconds
    scenario.condition_changes.push_back({10000000, NetworkCondition::LTE()});
    
    CCTestFramework test(CCAlgorithmFactory::BBRv2(), scenario);
    test.Run();
    test.PrintStats(true);
    
    auto metrics = test.GetMetrics();
    
    printf("\n--- Dynamic Network Performance ---\n");
    printf("Overall Throughput: %.2f Mbps\n", metrics.throughput_mbps);
    printf("Max Cwnd: %.2f KB\n", metrics.max_cwnd_bytes / 1024.0);
    
    EXPECT_GT(metrics.throughput_mbps, 5.0);
    EXPECT_GT(metrics.total_bytes_acked, 0UL);
}

}
} // namespace quic
} // namespace quicx
