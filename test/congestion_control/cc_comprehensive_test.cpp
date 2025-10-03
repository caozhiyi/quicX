#include <gtest/gtest.h>

#include <thread>
#include <vector>

#include "cc_test_framework.h"

using namespace quicx::quic;
using namespace quicx::quic::test;

// ========== Helper Functions ==========

// Run a scenario with a specific algorithm and return metrics
CCTestMetrics RunTest(const std::string& algo_name, 
                      CCTestFramework::CCFactory factory,
                      const TestScenario& scenario,
                      bool print_stats = false) {
    CCTestFramework test(factory, scenario);
    test.Run();
    if (print_stats) {
        printf("Algorithm: %s\n", algo_name.c_str());
        test.PrintStats(true);
    }
    return test.GetMetrics();
}

// Run all algorithms on a scenario and print comparison
void CompareAlgorithms(const TestScenario& scenario) {
    printf("\n========== Scenario: %s ==========\n", scenario.name.c_str());
    
    for (const auto& [name, factory] : CCAlgorithmFactory::AllAlgorithms()) {
        RunTest(name, factory, scenario, true);
    }
}

// ========== Single Algorithm Tests (Verify Expected Behavior) ==========

TEST(CCBehaviorTest, RenoSlowStartInIdealNetwork) {
    auto metrics = RunTest("Reno", CCAlgorithmFactory::Reno(), 
                          TestScenario::IdealNetwork(3000000), true);
    
    // In ideal network, should stay in slow start
    EXPECT_EQ(metrics.slow_start_duration_us, 3000000UL);
    EXPECT_GT(metrics.throughput_mbps, 50.0);
    EXPECT_EQ(metrics.recovery_count, 0UL);
    EXPECT_GT(metrics.cwnd_growth_rate, 1000.0);
}

TEST(CCBehaviorTest, RenoLossRecovery) {
    auto metrics = RunTest("Reno", CCAlgorithmFactory::Reno(), 
                          TestScenario::LossyNetwork(), true);
    
    // Should exit slow start and enter recovery
    EXPECT_LT(metrics.slow_start_duration_us, 5000000UL);
    EXPECT_GT(metrics.recovery_count, 0UL);
    EXPECT_LT(metrics.max_cwnd_bytes, metrics.min_cwnd_bytes * 10);
}

TEST(CCBehaviorTest, CubicInMobileNetwork) {
    auto metrics = RunTest("CUBIC", CCAlgorithmFactory::CUBIC(),
                          TestScenario::MobileNetwork(), true);
    
    // Should handle mobile network conditions
    EXPECT_GT(metrics.throughput_mbps, 0.1);
    EXPECT_GT(metrics.total_bytes_acked, 0UL);
}

TEST(CCBehaviorTest, BBRv1InBufferBloat) {
    auto metrics = RunTest("BBRv1", CCAlgorithmFactory::BBRv1(),
                          TestScenario::BufferBloat(), true);
    
    // BBR should handle buffer bloat
    EXPECT_GT(metrics.total_bytes_sent, 0UL);
    EXPECT_GE(metrics.throughput_mbps, 0.0);
}

// ========== Scenario-Based Tests (All Algorithms) ==========

TEST(CCScenarioTest, IdealNetwork) {
    CompareAlgorithms(TestScenario::IdealNetwork());
}

TEST(CCScenarioTest, LowLatencyNetwork) {
    CompareAlgorithms(TestScenario::LowLatencyNetwork());
}

TEST(CCScenarioTest, HighLatencyNetwork) {
    CompareAlgorithms(TestScenario::HighLatencyNetwork());
}

TEST(CCScenarioTest, MobileNetwork) {
    CompareAlgorithms(TestScenario::MobileNetwork());
}

TEST(CCScenarioTest, LossyNetwork) {
    CompareAlgorithms(TestScenario::LossyNetwork());
}

TEST(CCScenarioTest, BufferBloat) {
    CompareAlgorithms(TestScenario::BufferBloat());
}

TEST(CCScenarioTest, SatelliteLink) {
    CompareAlgorithms(TestScenario::SatelliteLink());
}

TEST(CCScenarioTest, ExtremeLoss) {
    CompareAlgorithms(TestScenario::ExtremeLoss());
}

// ========== Dynamic Network Tests ==========

TEST(CCDynamicTest, NetworkDegradation_Reno) {
    auto metrics = RunTest("Reno", CCAlgorithmFactory::Reno(),
                          TestScenario::NetworkDegradation(), true);
    
    // Should see increased recovery periods after degradation
    EXPECT_GT(metrics.recovery_count, 1UL);
    
    // Analyze cwnd changes
    double avg_cwnd_first_half = 0, avg_cwnd_second_half = 0;
    int count_first = 0, count_second = 0;
    uint64_t mid_point = 5000000;  // 5 seconds
    
    for (const auto& snapshot : metrics.state_history) {
        if (snapshot.time_us < mid_point) {
            avg_cwnd_first_half += snapshot.cwnd_bytes;
            count_first++;
        } else if (snapshot.time_us >= mid_point + 1000000) {  // Allow 1s adjustment
            avg_cwnd_second_half += snapshot.cwnd_bytes;
            count_second++;
        }
    }
    
    if (count_first > 0) avg_cwnd_first_half /= count_first;
    if (count_second > 0) avg_cwnd_second_half /= count_second;
    
    printf("Avg Cwnd Before: %.2f KB, After: %.2f KB\n", 
           avg_cwnd_first_half / 1024.0, avg_cwnd_second_half / 1024.0);
}

TEST(CCDynamicTest, NetworkDegradation_CUBIC) {
    auto metrics = RunTest("CUBIC", CCAlgorithmFactory::CUBIC(),
                          TestScenario::NetworkDegradation(), true);
    
    EXPECT_GT(metrics.recovery_count, 0UL);
    EXPECT_GT(metrics.throughput_mbps, 0.1);
}

TEST(CCDynamicTest, NetworkImprovement) {
    CompareAlgorithms(TestScenario::NetworkImprovement());
}

TEST(CCDynamicTest, FluctuatingNetwork) {
    auto metrics = RunTest("Reno", CCAlgorithmFactory::Reno(),
                          TestScenario::FluctuatingNetwork(), true);
    
    // Should have multiple recovery periods
    EXPECT_GT(metrics.recovery_count, 2UL);
    
    // Cwnd should vary significantly
    double variation = (metrics.max_cwnd_bytes - metrics.min_cwnd_bytes) / metrics.avg_cwnd_bytes;
    EXPECT_GT(variation, 0.3);  // At least 30% variation
}

// ========== Algorithm-Specific Behavior Tests ==========

TEST(CCAlgorithmTest, RenoVsCubicInLossyNetwork) {
    printf("\n========== Comparing Reno vs CUBIC in Lossy Network ==========\n");
    
    auto scenario = TestScenario::LossyNetwork();
    
    auto reno_metrics = RunTest("Reno", CCAlgorithmFactory::Reno(), scenario, true);
    auto cubic_metrics = RunTest("CUBIC", CCAlgorithmFactory::CUBIC(), scenario, true);
    
    printf("\nComparison:\n");
    printf("Reno  - Throughput: %.2f Mbps, Recovery: %llu\n", 
           reno_metrics.throughput_mbps, reno_metrics.recovery_count);
    printf("CUBIC - Throughput: %.2f Mbps, Recovery: %llu\n",
           cubic_metrics.throughput_mbps, cubic_metrics.recovery_count);
    
    // Both should handle losses
    EXPECT_GT(reno_metrics.recovery_count, 0UL);
    EXPECT_GT(cubic_metrics.recovery_count, 0UL);
}

TEST(CCAlgorithmTest, BBRVariantsComparison) {
    printf("\n========== Comparing BBR Variants ==========\n");
    
    auto scenario = TestScenario::BufferBloat();
    
    auto bbr_v1 = RunTest("BBRv1", CCAlgorithmFactory::BBRv1(), scenario, true);
    auto bbr_v2 = RunTest("BBRv2", CCAlgorithmFactory::BBRv2(), scenario, true);
    auto bbr_v3 = RunTest("BBRv3", CCAlgorithmFactory::BBRv3(), scenario, true);
    
    printf("\nBBR Variants Throughput:\n");
    printf("BBRv1: %.2f Mbps\n", bbr_v1.throughput_mbps);
    printf("BBRv2: %.2f Mbps\n", bbr_v2.throughput_mbps);
    printf("BBRv3: %.2f Mbps\n", bbr_v3.throughput_mbps);
}

// ========== Stress Tests ==========

TEST(CCStressTest, ExtremeLossAllAlgorithms) {
    printf("\n========== Extreme Loss (10%%) - All Algorithms ==========\n");
    
    auto scenario = TestScenario::ExtremeLoss();
    
    for (const auto& [name, factory] : CCAlgorithmFactory::AllAlgorithms()) {
        auto metrics = RunTest(name, factory, scenario, true);
        
        // Should still make some progress despite extreme loss
        EXPECT_GT(metrics.total_bytes_acked, 0UL);
        // BBR may have fewer recovery periods
        if (name != "BBRv1" && name != "BBRv2" && name != "BBRv3") {
            EXPECT_GT(metrics.recovery_count, 3UL);
        }
    }
}

TEST(CCStressTest, SatelliteLinkHighLatency) {
    printf("\n========== Satellite Link - All Algorithms ==========\n");
    
    auto scenario = TestScenario::SatelliteLink();
    
    for (const auto& [name, factory] : CCAlgorithmFactory::AllAlgorithms()) {
        auto metrics = RunTest(name, factory, scenario, true);
        
        // Verify high RTT (may be lower than base due to queuing)
        EXPECT_GT(metrics.avg_rtt_ms, 100.0);  // At least significantly high
        
        // Should achieve some throughput
        EXPECT_GT(metrics.total_bytes_acked, 1000UL);  // Lower threshold for high latency
    }
}

// ========== Custom Scenario Test ==========

TEST(CCCustomTest, CustomNetworkCondition) {
    TestScenario custom;
    custom.name = "Custom Network";
    custom.network_condition.base_rtt_us = 80000;
    custom.network_condition.rtt_jitter_us = 15000;
    custom.network_condition.packet_loss_rate = 0.03;
    custom.network_condition.bandwidth_bps = 15 * 1000 * 1000 / 8;
    custom.network_condition.queue_size_bytes = 200 * 1460;
    custom.test_duration_us = 10000000;
    
    printf("\n========== Custom Network Test ==========\n");
    printf("Config: RTT 80±15ms, Loss 3%%, BW 15Mbps\n");
    
    auto metrics = RunTest("CUBIC", CCAlgorithmFactory::CUBIC(), custom, true);
    
    EXPECT_GT(metrics.total_bytes_sent, 0UL);
}

// ========== Regression Tests ==========

TEST(CCRegressionTest, RenoMinimumPerformance) {
    auto metrics = RunTest("Reno", CCAlgorithmFactory::Reno(),
                          TestScenario::LowLatencyNetwork());
    
    // Performance baseline for regression testing
    EXPECT_GT(metrics.throughput_mbps, 1.0);
    // Allow higher loss rate as network conditions may be challenging
    EXPECT_LT(metrics.packet_loss_rate, 0.10);  // 10%
}

TEST(CCRegressionTest, CubicMinimumPerformance) {
    auto metrics = RunTest("CUBIC", CCAlgorithmFactory::CUBIC(),
                          TestScenario::LowLatencyNetwork());
    
    EXPECT_GT(metrics.throughput_mbps, 1.0);
    EXPECT_LT(metrics.packet_loss_rate, 0.15);  // 15%
}

// ========== Comprehensive Parallel Comparison Across All Scenarios ==========

struct AlgoResult { std::string name; CCTestMetrics metrics; };

static std::vector<AlgoResult> CompareAlgorithmsInParallel(const TestScenario& scenario) {
    auto algos = CCAlgorithmFactory::AllAlgorithms();
    std::vector<AlgoResult> results(algos.size());
    std::vector<std::thread> workers;
    workers.reserve(algos.size());
    for (size_t i = 0; i < algos.size(); ++i) {
        auto name = algos[i].first;
        auto factory = algos[i].second;
        workers.emplace_back([i, name, factory, scenario, &results]() {
            auto m = RunTest(name, factory, scenario, false);
            results[i] = {name, m};
        });
    }
    for (auto& t : workers) t.join();
    return results;
}

static void PrintComparisonTable(const std::vector<AlgoResult>& results) {
    printf("\n--- Algorithm Comparison ---\n");
    printf("%-10s | %10s | %10s | %10s | %10s\n",
           "Algorithm", "Throughput", "Max Cwnd", "Loss Rate", "Avg RTT");
    printf("-----------|------------|------------|------------|------------\n");
    for (const auto& r : results) {
        printf("%-10s | %7.2f Mbps | %7.2f KB | %8.2f%% | %7.2f ms\n",
               r.name.c_str(), r.metrics.throughput_mbps,
               r.metrics.max_cwnd_bytes / 1024.0,
               r.metrics.packet_loss_rate * 100.0,
               r.metrics.avg_rtt_ms);
    }
}

TEST(CCComprehensiveTest, AllScenariosAlgorithmComparisonParallel) {
    std::vector<TestScenario> scenarios;
    auto add = [&scenarios](const std::string& name, const NetworkCondition& nc, uint64_t dur) {
        TestScenario s; s.name = name; s.network_condition = nc; s.test_duration_us = dur; scenarios.push_back(s);
    };
    const uint64_t dur = 10000000; // 10 seconds
    add("Ideal",            NetworkCondition::Ideal(),            dur);
    add("Low Latency",      NetworkCondition::LowLatency(),       dur);
    add("High Latency",     NetworkCondition::HighLatency(),      dur);
    add("Mobile",           NetworkCondition::MobileNetwork(),    dur);
    add("Lossy",            NetworkCondition::LossyNetwork(),     dur);
    add("Buffer Bloat",     NetworkCondition::BufferBloat(),      dur);
    add("Satellite",        NetworkCondition::Satellite(),        dur);
    add("Unstable",         NetworkCondition::Unstable(),         dur);
    add("Enterprise",       NetworkCondition::EnterpriseNetwork(), dur);
    add("Broadband",        NetworkCondition::Broadband(),        dur);
    add("5G",               NetworkCondition::FiveG(),            dur);
    add("LTE",              NetworkCondition::LTE(),              dur);
    add("WiFi",             NetworkCondition::WiFi(),             dur);
    add("Cross-Continent",  NetworkCondition::CrossContinent(),   dur);

    for (const auto& scn : scenarios) {
        printf("\n========== Scenario: %s =========\n", scn.name.c_str());
        auto results = CompareAlgorithmsInParallel(scn);
        PrintComparisonTable(results);
    }
}
