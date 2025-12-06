#include <gtest/gtest.h>
#include <thread>
#include <vector>

#include "common/include/type.h"
#include "common/metrics/metrics.h"
#include "common/metrics/metrics_std.h"

namespace quicx {
namespace common {

class MetricsTest: public testing::Test {
protected:
    void SetUp() override {
        MetricsConfig config;
        config.enable = true;
        Metrics::Initialize(config);
    }
};

// Test 1: Standard Metrics
TEST_F(MetricsTest, StandardMetrics) {
    EXPECT_NE(MetricsStd::UdpPacketsRx, kInvalidMetricID);
    EXPECT_NE(MetricsStd::UdpPacketsTx, kInvalidMetricID);
    EXPECT_NE(MetricsStd::QuicConnectionsActive, kInvalidMetricID);

    Metrics::CounterInc(MetricsStd::UdpPacketsRx, 10);
    Metrics::CounterInc(MetricsStd::UdpPacketsTx, 5);

    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("udp_packets_rx 10"), std::string::npos);
    EXPECT_NE(output.find("udp_packets_tx 5"), std::string::npos);
}

// Test 2: Dynamic Registration
TEST_F(MetricsTest, DynamicRegistration) {
    auto id = Metrics::RegisterCounter("my_counter", "My custom counter");
    EXPECT_NE(id, kInvalidMetricID);

    Metrics::CounterInc(id, 42);

    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("my_counter 42"), std::string::npos);
    EXPECT_NE(output.find("# HELP my_counter My custom counter"), std::string::npos);
}

// Test 3: Labels
TEST_F(MetricsTest, Labels) {
    auto id1 = Metrics::RegisterCounter("http_req", "HTTP requests", {{"method", "GET"}});
    auto id2 = Metrics::RegisterCounter("http_req", "HTTP requests", {{"method", "POST"}});

    EXPECT_NE(id1, id2);

    Metrics::CounterInc(id1, 1);
    Metrics::CounterInc(id2, 2);

    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("http_req{method=\"GET\"} 1"), std::string::npos);
    EXPECT_NE(output.find("http_req{method=\"POST\"} 2"), std::string::npos);
}

// Test 4: Gauge Operations
TEST_F(MetricsTest, GaugeOperations) {
    auto id = Metrics::RegisterGauge("my_gauge", "My gauge");

    Metrics::GaugeInc(id, 10);
    Metrics::GaugeDec(id, 2);
    Metrics::GaugeSet(id, 100);

    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("my_gauge 100"), std::string::npos);
}

// Test 5: Histogram
TEST_F(MetricsTest, Histogram) {
    std::vector<uint64_t> buckets = {10, 20, 50};
    auto id = Metrics::RegisterHistogram("latency", "Latency histogram", buckets);

    Metrics::HistogramObserve(id, 5);   // bucket 0
    Metrics::HistogramObserve(id, 15);  // bucket 1
    Metrics::HistogramObserve(id, 25);  // bucket 2
    Metrics::HistogramObserve(id, 60);  // bucket 3 (+Inf)

    std::string output = Metrics::ExportPrometheus();
    // Cumulative counts
    EXPECT_NE(output.find("latency_bucket{le=\"10\"} 1"), std::string::npos);
    EXPECT_NE(output.find("latency_bucket{le=\"20\"} 2"), std::string::npos);
    EXPECT_NE(output.find("latency_bucket{le=\"50\"} 3"), std::string::npos);
    EXPECT_NE(output.find("latency_bucket{le=\"+Inf\"} 4"), std::string::npos);
    EXPECT_NE(output.find("latency_sum 105"), std::string::npos);
    EXPECT_NE(output.find("latency_count 4"), std::string::npos);
}

// Test 6: Multi-threaded Aggregation
TEST_F(MetricsTest, MultiThreaded) {
    auto id = Metrics::RegisterCounter("mt_counter", "Multi-threaded counter");

    auto worker = [id]() {
        for (int i = 0; i < 1000; ++i) {
            Metrics::CounterInc(id);
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back(worker);
    }

    for (auto& t : threads) {
        t.join();
    }

    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("mt_counter 10000"), std::string::npos);
}

// Test 7: Disable Config
TEST(MetricsDisableTest, DisableMetrics) {
    MetricsConfig config;
    config.enable = false;
    Metrics::Initialize(config);

    auto id = Metrics::RegisterCounter("disabled_counter");
    EXPECT_EQ(id, kInvalidMetricID);

    Metrics::CounterInc(kInvalidMetricID);

    std::string output = Metrics::ExportPrometheus();
    EXPECT_EQ(output, "");
}

// Test 8: Thread Exit (data merge)
TEST_F(MetricsTest, ThreadExit) {
    auto id = Metrics::RegisterCounter("thread_exit_counter", "Thread exit test");

    {
        std::thread t([id]() { Metrics::CounterInc(id, 100); });
        t.join();
        // Thread has exited, data should be merged to dead_counters_
    }

    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("thread_exit_counter 100"), std::string::npos);
}

// Test 9: Duplicate Registration
TEST_F(MetricsTest, DuplicateRegistration) {
    auto id1 = Metrics::RegisterCounter("dup_counter", "help");
    auto id2 = Metrics::RegisterCounter("dup_counter", "help");

    EXPECT_EQ(id1, id2);  // Should return same ID

    Metrics::CounterInc(id1, 5);
    Metrics::CounterInc(id2, 3);

    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("dup_counter 8"), std::string::npos);
}

}  // namespace common
}  // namespace quicx
