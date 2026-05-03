#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <string>
#include <sstream>
#include <regex>
#include <chrono>
#include <atomic>
#include <algorithm>

#include "common/include/type.h"
#include "common/metrics/metrics.h"
#include "common/metrics/metrics_std.h"

namespace quicx {
namespace common {

// ===========================================================================
// Test Fixture
// ===========================================================================

class MetricsComprehensiveTest : public testing::Test {
protected:
    void SetUp() override {
        MetricsConfig config;
        config.enable = true;
        Metrics::Initialize(config);
    }
};

// ===========================================================================
// 1. Standard Metrics Registration Completeness
// ===========================================================================

TEST_F(MetricsComprehensiveTest, AllUdpMetricsRegistered) {
    EXPECT_NE(MetricsStd::UdpPacketsRx, kInvalidMetricID);
    EXPECT_NE(MetricsStd::UdpPacketsTx, kInvalidMetricID);
    EXPECT_NE(MetricsStd::UdpBytesRx, kInvalidMetricID);
    EXPECT_NE(MetricsStd::UdpBytesTx, kInvalidMetricID);
    EXPECT_NE(MetricsStd::UdpDroppedPackets, kInvalidMetricID);
    EXPECT_NE(MetricsStd::UdpSendErrors, kInvalidMetricID);
}

TEST_F(MetricsComprehensiveTest, AllQuicConnectionMetricsRegistered) {
    EXPECT_NE(MetricsStd::QuicConnectionsActive, kInvalidMetricID);
    EXPECT_NE(MetricsStd::QuicConnectionsTotal, kInvalidMetricID);
    EXPECT_NE(MetricsStd::QuicConnectionsClosed, kInvalidMetricID);
    EXPECT_NE(MetricsStd::QuicHandshakeSuccess, kInvalidMetricID);
    EXPECT_NE(MetricsStd::QuicHandshakeFail, kInvalidMetricID);
    EXPECT_NE(MetricsStd::QuicHandshakeDurationUs, kInvalidMetricID);
}

TEST_F(MetricsComprehensiveTest, AllQuicPacketMetricsRegistered) {
    EXPECT_NE(MetricsStd::QuicPacketsRx, kInvalidMetricID);
    EXPECT_NE(MetricsStd::QuicPacketsTx, kInvalidMetricID);
    EXPECT_NE(MetricsStd::QuicPacketsRetransmit, kInvalidMetricID);
    EXPECT_NE(MetricsStd::QuicPacketsLost, kInvalidMetricID);
    EXPECT_NE(MetricsStd::QuicPacketsDropped, kInvalidMetricID);
    EXPECT_NE(MetricsStd::QuicPacketsAcked, kInvalidMetricID);
}

TEST_F(MetricsComprehensiveTest, AllQuicStreamMetricsRegistered) {
    EXPECT_NE(MetricsStd::QuicStreamsActive, kInvalidMetricID);
    EXPECT_NE(MetricsStd::QuicStreamsCreated, kInvalidMetricID);
    EXPECT_NE(MetricsStd::QuicStreamsClosed, kInvalidMetricID);
    EXPECT_NE(MetricsStd::QuicStreamsBytesRx, kInvalidMetricID);
    EXPECT_NE(MetricsStd::QuicStreamsBytesTx, kInvalidMetricID);
    EXPECT_NE(MetricsStd::QuicStreamsResetRx, kInvalidMetricID);
    EXPECT_NE(MetricsStd::QuicStreamsResetTx, kInvalidMetricID);
}

TEST_F(MetricsComprehensiveTest, AllFlowControlMetricsRegistered) {
    EXPECT_NE(MetricsStd::QuicFlowControlBlocked, kInvalidMetricID);
    EXPECT_NE(MetricsStd::QuicStreamDataBlocked, kInvalidMetricID);
}

TEST_F(MetricsComprehensiveTest, AllHttp3MetricsRegistered) {
    EXPECT_NE(MetricsStd::Http3RequestsTotal, kInvalidMetricID);
    EXPECT_NE(MetricsStd::Http3RequestsActive, kInvalidMetricID);
    EXPECT_NE(MetricsStd::Http3RequestsFailed, kInvalidMetricID);
    EXPECT_NE(MetricsStd::Http3RequestDurationUs, kInvalidMetricID);
    EXPECT_NE(MetricsStd::Http3ResponseBytesRx, kInvalidMetricID);
    EXPECT_NE(MetricsStd::Http3ResponseBytesTx, kInvalidMetricID);
    EXPECT_NE(MetricsStd::Http3PushPromisesRx, kInvalidMetricID);
    EXPECT_NE(MetricsStd::Http3PushPromisesTx, kInvalidMetricID);
}

TEST_F(MetricsComprehensiveTest, AllCongestionControlMetricsRegistered) {
    EXPECT_NE(MetricsStd::CongestionWindowBytes, kInvalidMetricID);
    EXPECT_NE(MetricsStd::CongestionEventsTotal, kInvalidMetricID);
    EXPECT_NE(MetricsStd::SlowStartExits, kInvalidMetricID);
    EXPECT_NE(MetricsStd::BytesInFlight, kInvalidMetricID);
}

TEST_F(MetricsComprehensiveTest, AllPerformanceMetricsRegistered) {
    EXPECT_NE(MetricsStd::RttSmoothedUs, kInvalidMetricID);
    EXPECT_NE(MetricsStd::RttVarianceUs, kInvalidMetricID);
    EXPECT_NE(MetricsStd::RttMinUs, kInvalidMetricID);
    EXPECT_NE(MetricsStd::PacketProcessTimeUs, kInvalidMetricID);
}

TEST_F(MetricsComprehensiveTest, AllMemoryMetricsRegistered) {
    EXPECT_NE(MetricsStd::MemPoolAllocatedBlocks, kInvalidMetricID);
    EXPECT_NE(MetricsStd::MemPoolFreeBlocks, kInvalidMetricID);
    EXPECT_NE(MetricsStd::MemPoolAllocations, kInvalidMetricID);
    EXPECT_NE(MetricsStd::MemPoolDeallocations, kInvalidMetricID);
}

TEST_F(MetricsComprehensiveTest, AllErrorMetricsRegistered) {
    EXPECT_NE(MetricsStd::ErrorsProtocol, kInvalidMetricID);
    EXPECT_NE(MetricsStd::ErrorsInternal, kInvalidMetricID);
    EXPECT_NE(MetricsStd::ErrorsFlowControl, kInvalidMetricID);
    EXPECT_NE(MetricsStd::ErrorsStreamLimit, kInvalidMetricID);
}

TEST_F(MetricsComprehensiveTest, All0RttMetricsRegistered) {
    EXPECT_NE(MetricsStd::Quic0RttAccepted, kInvalidMetricID);
    EXPECT_NE(MetricsStd::Quic0RttRejected, kInvalidMetricID);
    EXPECT_NE(MetricsStd::Quic0RttBytesRx, kInvalidMetricID);
    EXPECT_NE(MetricsStd::Quic0RttBytesTx, kInvalidMetricID);
}

TEST_F(MetricsComprehensiveTest, AllPathMtuMetricsRegistered) {
    EXPECT_NE(MetricsStd::PathMtuCurrent, kInvalidMetricID);
    EXPECT_NE(MetricsStd::PathMtuUpdates, kInvalidMetricID);
}

TEST_F(MetricsComprehensiveTest, AllMigrationMetricsRegistered) {
    EXPECT_NE(MetricsStd::ConnectionMigrationsTotal, kInvalidMetricID);
    EXPECT_NE(MetricsStd::ConnectionMigrationsFailed, kInvalidMetricID);
}

TEST_F(MetricsComprehensiveTest, AllTlsMetricsRegistered) {
    EXPECT_NE(MetricsStd::TlsHandshakeDurationUs, kInvalidMetricID);
    EXPECT_NE(MetricsStd::TlsSessionsResumed, kInvalidMetricID);
    EXPECT_NE(MetricsStd::TlsSessionsCached, kInvalidMetricID);
}

TEST_F(MetricsComprehensiveTest, AllFrameMetricsRegistered) {
    EXPECT_NE(MetricsStd::FramesRxTotal, kInvalidMetricID);
    EXPECT_NE(MetricsStd::FramesTxTotal, kInvalidMetricID);
}

TEST_F(MetricsComprehensiveTest, AllHttp3StatusCodeMetricsRegistered) {
    EXPECT_NE(MetricsStd::Http3Responses2xx, kInvalidMetricID);
    EXPECT_NE(MetricsStd::Http3Responses3xx, kInvalidMetricID);
    EXPECT_NE(MetricsStd::Http3Responses4xx, kInvalidMetricID);
    EXPECT_NE(MetricsStd::Http3Responses5xx, kInvalidMetricID);
}

TEST_F(MetricsComprehensiveTest, AllPacingMetricsRegistered) {
    EXPECT_NE(MetricsStd::PacingRateBytesPerSec, kInvalidMetricID);
    EXPECT_NE(MetricsStd::PacingDelayUs, kInvalidMetricID);
}

TEST_F(MetricsComprehensiveTest, AllAckMetricsRegistered) {
    EXPECT_NE(MetricsStd::AckDelayUs, kInvalidMetricID);
    EXPECT_NE(MetricsStd::AckRangesPerFrame, kInvalidMetricID);
    EXPECT_NE(MetricsStd::AckFrequency, kInvalidMetricID);
}

TEST_F(MetricsComprehensiveTest, AllTimeoutMetricsRegistered) {
    EXPECT_NE(MetricsStd::IdleTimeoutTotal, kInvalidMetricID);
    EXPECT_NE(MetricsStd::PtoCountTotal, kInvalidMetricID);
    EXPECT_NE(MetricsStd::PtoCountPerConnection, kInvalidMetricID);
}

TEST_F(MetricsComprehensiveTest, AllVersionNegotiationMetricsRegistered) {
    EXPECT_NE(MetricsStd::VersionNegotiationTotal, kInvalidMetricID);
    EXPECT_NE(MetricsStd::QuicVersionInUse, kInvalidMetricID);
}

TEST_F(MetricsComprehensiveTest, AllRetryMetricsRegistered) {
    EXPECT_NE(MetricsStd::QuicRetryPacketsSent, kInvalidMetricID);
    EXPECT_NE(MetricsStd::QuicRetryByHighRate, kInvalidMetricID);
    EXPECT_NE(MetricsStd::QuicRetryBySuspiciousIP, kInvalidMetricID);
    EXPECT_NE(MetricsStd::QuicRetryByPolicy, kInvalidMetricID);
    EXPECT_NE(MetricsStd::QuicRetryTokensValidated, kInvalidMetricID);
    EXPECT_NE(MetricsStd::QuicRetryTokensInvalid, kInvalidMetricID);
}

// Verify all 82 metric IDs are unique
TEST_F(MetricsComprehensiveTest, AllMetricIDsUnique) {
    std::vector<MetricID> all_ids = {
        MetricsStd::UdpPacketsRx, MetricsStd::UdpPacketsTx, MetricsStd::UdpBytesRx,
        MetricsStd::UdpBytesTx, MetricsStd::UdpDroppedPackets, MetricsStd::UdpSendErrors,
        MetricsStd::QuicConnectionsActive, MetricsStd::QuicConnectionsTotal,
        MetricsStd::QuicConnectionsClosed, MetricsStd::QuicHandshakeSuccess,
        MetricsStd::QuicHandshakeFail, MetricsStd::QuicHandshakeDurationUs,
        MetricsStd::QuicPacketsRx, MetricsStd::QuicPacketsTx,
        MetricsStd::QuicPacketsRetransmit, MetricsStd::QuicPacketsLost,
        MetricsStd::QuicPacketsDropped, MetricsStd::QuicPacketsAcked,
        MetricsStd::QuicStreamsActive, MetricsStd::QuicStreamsCreated,
        MetricsStd::QuicStreamsClosed, MetricsStd::QuicStreamsBytesRx,
        MetricsStd::QuicStreamsBytesTx, MetricsStd::QuicStreamsResetRx,
        MetricsStd::QuicStreamsResetTx,
        MetricsStd::QuicFlowControlBlocked, MetricsStd::QuicStreamDataBlocked,
        MetricsStd::Http3RequestsTotal, MetricsStd::Http3RequestsActive,
        MetricsStd::Http3RequestsFailed, MetricsStd::Http3RequestDurationUs,
        MetricsStd::Http3ResponseBytesRx, MetricsStd::Http3ResponseBytesTx,
        MetricsStd::Http3PushPromisesRx, MetricsStd::Http3PushPromisesTx,
        MetricsStd::CongestionWindowBytes, MetricsStd::CongestionEventsTotal,
        MetricsStd::SlowStartExits, MetricsStd::BytesInFlight,
        MetricsStd::RttSmoothedUs, MetricsStd::RttVarianceUs, MetricsStd::RttMinUs,
        MetricsStd::PacketProcessTimeUs,
        MetricsStd::MemPoolAllocatedBlocks, MetricsStd::MemPoolFreeBlocks,
        MetricsStd::MemPoolAllocations, MetricsStd::MemPoolDeallocations,
        MetricsStd::ErrorsProtocol, MetricsStd::ErrorsInternal,
        MetricsStd::ErrorsFlowControl, MetricsStd::ErrorsStreamLimit,
        MetricsStd::Quic0RttAccepted, MetricsStd::Quic0RttRejected,
        MetricsStd::Quic0RttBytesRx, MetricsStd::Quic0RttBytesTx,
        MetricsStd::PathMtuCurrent, MetricsStd::PathMtuUpdates,
        MetricsStd::ConnectionMigrationsTotal, MetricsStd::ConnectionMigrationsFailed,
        MetricsStd::TlsHandshakeDurationUs, MetricsStd::TlsSessionsResumed,
        MetricsStd::TlsSessionsCached,
        MetricsStd::FramesRxTotal, MetricsStd::FramesTxTotal,
        MetricsStd::Http3Responses2xx, MetricsStd::Http3Responses3xx,
        MetricsStd::Http3Responses4xx, MetricsStd::Http3Responses5xx,
        MetricsStd::PacingRateBytesPerSec, MetricsStd::PacingDelayUs,
        MetricsStd::AckDelayUs, MetricsStd::AckRangesPerFrame, MetricsStd::AckFrequency,
        MetricsStd::IdleTimeoutTotal, MetricsStd::PtoCountTotal,
        MetricsStd::PtoCountPerConnection,
        MetricsStd::VersionNegotiationTotal, MetricsStd::QuicVersionInUse,
        MetricsStd::QuicRetryPacketsSent, MetricsStd::QuicRetryByHighRate,
        MetricsStd::QuicRetryBySuspiciousIP, MetricsStd::QuicRetryByPolicy,
        MetricsStd::QuicRetryTokensValidated, MetricsStd::QuicRetryTokensInvalid,
    };

    // All should be valid
    for (size_t i = 0; i < all_ids.size(); ++i) {
        EXPECT_NE(all_ids[i], kInvalidMetricID) << "MetricID at index " << i << " is invalid";
    }

    // All should be unique
    std::vector<MetricID> sorted_ids = all_ids;
    std::sort(sorted_ids.begin(), sorted_ids.end());
    auto last = std::unique(sorted_ids.begin(), sorted_ids.end());
    EXPECT_EQ(last, sorted_ids.end()) << "Duplicate MetricIDs found among standard metrics";

    // Expected count: 84 standard metrics
    EXPECT_EQ(all_ids.size(), 84u) << "Expected 84 standard metrics";
}

// ===========================================================================
// 2. Counter Operations
// ===========================================================================

TEST_F(MetricsComprehensiveTest, CounterIncByOne) {
    auto id = Metrics::RegisterCounter("test_counter_inc1", "Test counter");
    Metrics::CounterInc(id);
    Metrics::CounterInc(id);
    Metrics::CounterInc(id);

    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("test_counter_inc1 3"), std::string::npos);
}

TEST_F(MetricsComprehensiveTest, CounterIncByValue) {
    auto id = Metrics::RegisterCounter("test_counter_inc_val", "Test counter value");
    Metrics::CounterInc(id, 100);
    Metrics::CounterInc(id, 200);
    Metrics::CounterInc(id, 300);

    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("test_counter_inc_val 600"), std::string::npos);
}

TEST_F(MetricsComprehensiveTest, CounterIncLargeValue) {
    auto id = Metrics::RegisterCounter("test_counter_large", "Large counter");
    uint64_t large_val = 1000000000ULL;  // 1 billion
    Metrics::CounterInc(id, large_val);
    Metrics::CounterInc(id, large_val);

    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("test_counter_large 2000000000"), std::string::npos);
}

TEST_F(MetricsComprehensiveTest, CounterWithInvalidID) {
    // Should not crash when using invalid ID
    Metrics::CounterInc(kInvalidMetricID, 1);
    Metrics::CounterInc(kInvalidMetricID, 100);
    // No assertion needed - just ensure no crash
}

// ===========================================================================
// 3. Gauge Operations
// ===========================================================================

TEST_F(MetricsComprehensiveTest, GaugeIncDec) {
    auto id = Metrics::RegisterGauge("test_gauge_incdec", "Test gauge");
    Metrics::GaugeInc(id, 50);
    Metrics::GaugeDec(id, 20);

    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("test_gauge_incdec 30"), std::string::npos);
}

TEST_F(MetricsComprehensiveTest, GaugeSetOverwrite) {
    auto id = Metrics::RegisterGauge("test_gauge_set", "Test gauge set");
    Metrics::GaugeInc(id, 999);
    Metrics::GaugeSet(id, 42);

    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("test_gauge_set 42"), std::string::npos);
}

TEST_F(MetricsComprehensiveTest, GaugeNegativeValue) {
    auto id = Metrics::RegisterGauge("test_gauge_neg", "Test gauge negative");
    Metrics::GaugeDec(id, 10);

    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("test_gauge_neg -10"), std::string::npos);
}

TEST_F(MetricsComprehensiveTest, GaugeSetZero) {
    auto id = Metrics::RegisterGauge("test_gauge_zero", "Test gauge zero");
    Metrics::GaugeInc(id, 100);
    Metrics::GaugeSet(id, 0);

    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("test_gauge_zero 0"), std::string::npos);
}

TEST_F(MetricsComprehensiveTest, GaugeWithInvalidID) {
    // Should not crash
    Metrics::GaugeInc(kInvalidMetricID);
    Metrics::GaugeDec(kInvalidMetricID);
    Metrics::GaugeSet(kInvalidMetricID, 42);
}

// ===========================================================================
// 4. Histogram Operations
// ===========================================================================

TEST_F(MetricsComprehensiveTest, HistogramBucketDistribution) {
    std::vector<uint64_t> buckets = {100, 500, 1000, 5000, 10000};
    auto id = Metrics::RegisterHistogram("test_hist_dist", "Test histogram", buckets);

    // Values: 50 (bucket 0), 200 (bucket 1), 800 (bucket 2), 3000 (bucket 3), 7000 (bucket 4), 20000 (+Inf)
    Metrics::HistogramObserve(id, 50);
    Metrics::HistogramObserve(id, 200);
    Metrics::HistogramObserve(id, 800);
    Metrics::HistogramObserve(id, 3000);
    Metrics::HistogramObserve(id, 7000);
    Metrics::HistogramObserve(id, 20000);

    std::string output = Metrics::ExportPrometheus();

    // Cumulative counts
    EXPECT_NE(output.find("test_hist_dist_bucket{le=\"100\"} 1"), std::string::npos);
    EXPECT_NE(output.find("test_hist_dist_bucket{le=\"500\"} 2"), std::string::npos);
    EXPECT_NE(output.find("test_hist_dist_bucket{le=\"1000\"} 3"), std::string::npos);
    EXPECT_NE(output.find("test_hist_dist_bucket{le=\"5000\"} 4"), std::string::npos);
    EXPECT_NE(output.find("test_hist_dist_bucket{le=\"10000\"} 5"), std::string::npos);
    EXPECT_NE(output.find("test_hist_dist_bucket{le=\"+Inf\"} 6"), std::string::npos);

    // Sum = 50 + 200 + 800 + 3000 + 7000 + 20000 = 31050
    EXPECT_NE(output.find("test_hist_dist_sum 31050"), std::string::npos);
    EXPECT_NE(output.find("test_hist_dist_count 6"), std::string::npos);
}

TEST_F(MetricsComprehensiveTest, HistogramAllInFirstBucket) {
    std::vector<uint64_t> buckets = {100, 200, 300};
    auto id = Metrics::RegisterHistogram("test_hist_first", "Test all first", buckets);

    Metrics::HistogramObserve(id, 10);
    Metrics::HistogramObserve(id, 20);
    Metrics::HistogramObserve(id, 50);

    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("test_hist_first_bucket{le=\"100\"} 3"), std::string::npos);
    EXPECT_NE(output.find("test_hist_first_bucket{le=\"200\"} 3"), std::string::npos);
    EXPECT_NE(output.find("test_hist_first_bucket{le=\"300\"} 3"), std::string::npos);
    EXPECT_NE(output.find("test_hist_first_bucket{le=\"+Inf\"} 3"), std::string::npos);
    EXPECT_NE(output.find("test_hist_first_count 3"), std::string::npos);
}

TEST_F(MetricsComprehensiveTest, HistogramAllInInfBucket) {
    std::vector<uint64_t> buckets = {10, 20, 30};
    auto id = Metrics::RegisterHistogram("test_hist_inf", "Test all inf", buckets);

    Metrics::HistogramObserve(id, 100);
    Metrics::HistogramObserve(id, 200);

    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("test_hist_inf_bucket{le=\"10\"} 0"), std::string::npos);
    EXPECT_NE(output.find("test_hist_inf_bucket{le=\"20\"} 0"), std::string::npos);
    EXPECT_NE(output.find("test_hist_inf_bucket{le=\"30\"} 0"), std::string::npos);
    EXPECT_NE(output.find("test_hist_inf_bucket{le=\"+Inf\"} 2"), std::string::npos);
    EXPECT_NE(output.find("test_hist_inf_sum 300"), std::string::npos);
    EXPECT_NE(output.find("test_hist_inf_count 2"), std::string::npos);
}

TEST_F(MetricsComprehensiveTest, HistogramBoundaryValues) {
    std::vector<uint64_t> buckets = {10, 20, 30};
    auto id = Metrics::RegisterHistogram("test_hist_boundary", "Boundary test", buckets);

    // Values exactly on bucket boundaries should go into that bucket
    Metrics::HistogramObserve(id, 10);  // <= 10 → bucket 0
    Metrics::HistogramObserve(id, 20);  // <= 20 → bucket 1
    Metrics::HistogramObserve(id, 30);  // <= 30 → bucket 2

    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("test_hist_boundary_bucket{le=\"10\"} 1"), std::string::npos);
    EXPECT_NE(output.find("test_hist_boundary_bucket{le=\"20\"} 2"), std::string::npos);
    EXPECT_NE(output.find("test_hist_boundary_bucket{le=\"30\"} 3"), std::string::npos);
    EXPECT_NE(output.find("test_hist_boundary_bucket{le=\"+Inf\"} 3"), std::string::npos);
}

TEST_F(MetricsComprehensiveTest, HistogramZeroValue) {
    std::vector<uint64_t> buckets = {10, 100};
    auto id = Metrics::RegisterHistogram("test_hist_zero", "Zero observation", buckets);

    Metrics::HistogramObserve(id, 0);

    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("test_hist_zero_bucket{le=\"10\"} 1"), std::string::npos);
    EXPECT_NE(output.find("test_hist_zero_sum 0"), std::string::npos);
    EXPECT_NE(output.find("test_hist_zero_count 1"), std::string::npos);
}

// ===========================================================================
// 5. Standard Metrics Counter Operations
// ===========================================================================

TEST_F(MetricsComprehensiveTest, StandardCounterUdpMetrics) {
    Metrics::CounterInc(MetricsStd::UdpPacketsRx, 100);
    Metrics::CounterInc(MetricsStd::UdpPacketsTx, 80);
    Metrics::CounterInc(MetricsStd::UdpBytesRx, 150000);
    Metrics::CounterInc(MetricsStd::UdpBytesTx, 120000);
    Metrics::CounterInc(MetricsStd::UdpDroppedPackets, 3);
    Metrics::CounterInc(MetricsStd::UdpSendErrors, 1);

    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("udp_packets_rx"), std::string::npos);
    EXPECT_NE(output.find("udp_packets_tx"), std::string::npos);
    EXPECT_NE(output.find("udp_bytes_rx"), std::string::npos);
    EXPECT_NE(output.find("udp_bytes_tx"), std::string::npos);
    EXPECT_NE(output.find("udp_dropped_packets"), std::string::npos);
    EXPECT_NE(output.find("udp_send_errors"), std::string::npos);
}

TEST_F(MetricsComprehensiveTest, StandardGaugeCongestionMetrics) {
    Metrics::GaugeSet(MetricsStd::CongestionWindowBytes, 65535);
    Metrics::GaugeSet(MetricsStd::BytesInFlight, 32000);

    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("congestion_window_bytes 65535"), std::string::npos);
    EXPECT_NE(output.find("bytes_in_flight 32000"), std::string::npos);
}

TEST_F(MetricsComprehensiveTest, StandardGaugeRttMetrics) {
    Metrics::GaugeSet(MetricsStd::RttSmoothedUs, 15000);
    Metrics::GaugeSet(MetricsStd::RttVarianceUs, 2000);
    Metrics::GaugeSet(MetricsStd::RttMinUs, 5000);

    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("rtt_smoothed_us 15000"), std::string::npos);
    EXPECT_NE(output.find("rtt_variance_us 2000"), std::string::npos);
    EXPECT_NE(output.find("rtt_min_us 5000"), std::string::npos);
}

TEST_F(MetricsComprehensiveTest, StandardHistogramHandshakeDuration) {
    // Observe handshake durations in various buckets
    Metrics::HistogramObserve(MetricsStd::QuicHandshakeDurationUs, 800);     // < 1000
    Metrics::HistogramObserve(MetricsStd::QuicHandshakeDurationUs, 3000);    // < 5000
    Metrics::HistogramObserve(MetricsStd::QuicHandshakeDurationUs, 8000);    // < 10000
    Metrics::HistogramObserve(MetricsStd::QuicHandshakeDurationUs, 200000);  // < 500000
    Metrics::HistogramObserve(MetricsStd::QuicHandshakeDurationUs, 2000000); // > 1000000 → +Inf

    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("quic_handshake_duration_us_bucket"), std::string::npos);
    EXPECT_NE(output.find("quic_handshake_duration_us_count 5"), std::string::npos);
}

TEST_F(MetricsComprehensiveTest, StandardRetryMetrics) {
    Metrics::CounterInc(MetricsStd::QuicRetryPacketsSent, 10);
    Metrics::CounterInc(MetricsStd::QuicRetryByHighRate, 3);
    Metrics::CounterInc(MetricsStd::QuicRetryBySuspiciousIP, 2);
    Metrics::CounterInc(MetricsStd::QuicRetryByPolicy, 5);
    Metrics::CounterInc(MetricsStd::QuicRetryTokensValidated, 8);
    Metrics::CounterInc(MetricsStd::QuicRetryTokensInvalid, 2);

    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("quic_retry_packets_sent 10"), std::string::npos);
    EXPECT_NE(output.find("quic_retry_by_high_rate 3"), std::string::npos);
    EXPECT_NE(output.find("quic_retry_tokens_validated 8"), std::string::npos);
}

// ===========================================================================
// 6. Multi-threaded Operations
// ===========================================================================

TEST_F(MetricsComprehensiveTest, MultiThreadedGaugeAggregation) {
    auto id = Metrics::RegisterGauge("mt_gauge", "MT gauge");

    auto worker = [id]() {
        for (int i = 0; i < 500; ++i) {
            Metrics::GaugeInc(id);
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) {
        t.join();
    }

    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("mt_gauge 4000"), std::string::npos);
}

TEST_F(MetricsComprehensiveTest, MultiThreadedHistogramAggregation) {
    std::vector<uint64_t> buckets = {100, 500, 1000};
    auto id = Metrics::RegisterHistogram("mt_histogram", "MT histogram", buckets);

    auto worker = [id]() {
        for (int i = 0; i < 200; ++i) {
            Metrics::HistogramObserve(id, 50);  // All go to bucket 0 (<=100)
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) {
        t.join();
    }

    std::string output = Metrics::ExportPrometheus();
    // 8 threads x 200 observations = 1600
    EXPECT_NE(output.find("mt_histogram_count 1600"), std::string::npos);
    // Sum = 1600 * 50 = 80000
    EXPECT_NE(output.find("mt_histogram_sum 80000"), std::string::npos);
    // All in first bucket
    EXPECT_NE(output.find("mt_histogram_bucket{le=\"100\"} 1600"), std::string::npos);
}

TEST_F(MetricsComprehensiveTest, MultiThreadedMixedOperations) {
    auto counter_id = Metrics::RegisterCounter("mt_mixed_counter", "MT mixed counter");
    auto gauge_id = Metrics::RegisterGauge("mt_mixed_gauge", "MT mixed gauge");

    auto counter_worker = [counter_id]() {
        for (int i = 0; i < 1000; ++i) {
            Metrics::CounterInc(counter_id);
        }
    };

    auto gauge_worker = [gauge_id]() {
        for (int i = 0; i < 500; ++i) {
            Metrics::GaugeInc(gauge_id);
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back(counter_worker);
    }
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back(gauge_worker);
    }
    for (auto& t : threads) {
        t.join();
    }

    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("mt_mixed_counter 4000"), std::string::npos);
    EXPECT_NE(output.find("mt_mixed_gauge 2000"), std::string::npos);
}

// ===========================================================================
// 7. Thread Exit Data Merge (all types)
// ===========================================================================

TEST_F(MetricsComprehensiveTest, ThreadExitGaugeMerge) {
    auto id = Metrics::RegisterGauge("thread_exit_gauge", "Thread exit gauge test");

    {
        std::thread t([id]() {
            Metrics::GaugeInc(id, 50);
        });
        t.join();
    }

    // After thread exit, gauge data should be merged
    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("thread_exit_gauge 50"), std::string::npos);
}

TEST_F(MetricsComprehensiveTest, ThreadExitHistogramMerge) {
    std::vector<uint64_t> buckets = {10, 100};
    auto id = Metrics::RegisterHistogram("thread_exit_hist", "Thread exit histogram test", buckets);

    {
        std::thread t([id]() {
            Metrics::HistogramObserve(id, 5);
            Metrics::HistogramObserve(id, 50);
            Metrics::HistogramObserve(id, 200);
        });
        t.join();
    }

    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("thread_exit_hist_count 3"), std::string::npos);
    EXPECT_NE(output.find("thread_exit_hist_sum 255"), std::string::npos);
    EXPECT_NE(output.find("thread_exit_hist_bucket{le=\"10\"} 1"), std::string::npos);
    EXPECT_NE(output.find("thread_exit_hist_bucket{le=\"100\"} 2"), std::string::npos);
    EXPECT_NE(output.find("thread_exit_hist_bucket{le=\"+Inf\"} 3"), std::string::npos);
}

TEST_F(MetricsComprehensiveTest, MultipleThreadsExitAndMerge) {
    auto id = Metrics::RegisterCounter("multi_exit_counter", "Multi thread exit test");

    for (int i = 0; i < 5; ++i) {
        std::thread t([id]() {
            Metrics::CounterInc(id, 100);
        });
        t.join();
    }

    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("multi_exit_counter 500"), std::string::npos);
}

// ===========================================================================
// 8. Prometheus Export Format Validation
// ===========================================================================

TEST_F(MetricsComprehensiveTest, PrometheusCounterFormat) {
    auto id = Metrics::RegisterCounter("prom_fmt_counter", "A counter for format check");
    Metrics::CounterInc(id, 42);

    std::string output = Metrics::ExportPrometheus();

    // Must have HELP line
    EXPECT_NE(output.find("# HELP prom_fmt_counter A counter for format check"), std::string::npos);
    // Must have TYPE line
    EXPECT_NE(output.find("# TYPE prom_fmt_counter counter"), std::string::npos);
    // Must have value line
    EXPECT_NE(output.find("prom_fmt_counter 42"), std::string::npos);
}

TEST_F(MetricsComprehensiveTest, PrometheusGaugeFormat) {
    auto id = Metrics::RegisterGauge("prom_fmt_gauge", "A gauge for format check");
    Metrics::GaugeSet(id, 99);

    std::string output = Metrics::ExportPrometheus();

    EXPECT_NE(output.find("# HELP prom_fmt_gauge A gauge for format check"), std::string::npos);
    EXPECT_NE(output.find("# TYPE prom_fmt_gauge gauge"), std::string::npos);
    EXPECT_NE(output.find("prom_fmt_gauge 99"), std::string::npos);
}

TEST_F(MetricsComprehensiveTest, PrometheusHistogramFormat) {
    std::vector<uint64_t> buckets = {50, 100};
    auto id = Metrics::RegisterHistogram("prom_fmt_hist", "A histogram for format check", buckets);
    Metrics::HistogramObserve(id, 25);

    std::string output = Metrics::ExportPrometheus();

    EXPECT_NE(output.find("# HELP prom_fmt_hist A histogram for format check"), std::string::npos);
    EXPECT_NE(output.find("# TYPE prom_fmt_hist histogram"), std::string::npos);
    EXPECT_NE(output.find("prom_fmt_hist_bucket{le=\"50\"} 1"), std::string::npos);
    EXPECT_NE(output.find("prom_fmt_hist_bucket{le=\"100\"} 1"), std::string::npos);
    EXPECT_NE(output.find("prom_fmt_hist_bucket{le=\"+Inf\"} 1"), std::string::npos);
    EXPECT_NE(output.find("prom_fmt_hist_sum 25"), std::string::npos);
    EXPECT_NE(output.find("prom_fmt_hist_count 1"), std::string::npos);
}

TEST_F(MetricsComprehensiveTest, PrometheusLabelsFormat) {
    auto id = Metrics::RegisterCounter(
        "prom_fmt_labeled", "Labeled counter",
        {{"method", "GET"}, {"status", "200"}});
    Metrics::CounterInc(id, 5);

    std::string output = Metrics::ExportPrometheus();
    // Labels should be in format: {key="value",key="value"}
    EXPECT_NE(output.find("prom_fmt_labeled{"), std::string::npos);
    EXPECT_NE(output.find("method=\"GET\""), std::string::npos);
    EXPECT_NE(output.find("status=\"200\""), std::string::npos);
}

TEST_F(MetricsComprehensiveTest, PrometheusHistogramWithLabels) {
    std::vector<uint64_t> buckets = {10, 100};
    auto id = Metrics::RegisterHistogram(
        "prom_fmt_hist_labels", "Labeled histogram", buckets,
        {{"endpoint", "/api"}});
    Metrics::HistogramObserve(id, 50);

    std::string output = Metrics::ExportPrometheus();
    // Bucket labels should combine existing labels with "le"
    EXPECT_NE(output.find("endpoint=\"/api\""), std::string::npos);
    EXPECT_NE(output.find("le=\"10\""), std::string::npos);
    EXPECT_NE(output.find("le=\"+Inf\""), std::string::npos);
}

// ===========================================================================
// 9. Edge Cases / Boundary Conditions
// ===========================================================================

TEST_F(MetricsComprehensiveTest, EmptyLabels) {
    auto id = Metrics::RegisterCounter("no_labels", "No labels counter", {});
    Metrics::CounterInc(id, 1);

    std::string output = Metrics::ExportPrometheus();
    // Should have metric name without label braces
    EXPECT_NE(output.find("no_labels 1"), std::string::npos);
}

TEST_F(MetricsComprehensiveTest, ManyLabels) {
    auto id = Metrics::RegisterCounter(
        "many_labels", "Many labels counter",
        {{"a", "1"}, {"b", "2"}, {"c", "3"}, {"d", "4"}});
    Metrics::CounterInc(id, 1);

    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("many_labels{"), std::string::npos);
}

TEST_F(MetricsComprehensiveTest, SameNameDifferentLabelsAreDifferent) {
    auto id1 = Metrics::RegisterCounter("same_name_metric", "help", {{"env", "prod"}});
    auto id2 = Metrics::RegisterCounter("same_name_metric", "help", {{"env", "dev"}});

    EXPECT_NE(id1, id2);

    Metrics::CounterInc(id1, 10);
    Metrics::CounterInc(id2, 20);

    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("env=\"prod\""), std::string::npos);
    EXPECT_NE(output.find("env=\"dev\""), std::string::npos);
}

TEST_F(MetricsComprehensiveTest, ExportWithNoMetricUpdates) {
    // After initialization, export without any updates should return standard metrics with zero values
    std::string output = Metrics::ExportPrometheus();
    // Should contain standard metric names
    EXPECT_NE(output.find("udp_packets_rx"), std::string::npos);
    EXPECT_NE(output.find("quic_connections_active"), std::string::npos);
}

// ===========================================================================
// 10. Realistic Scenario: Simulate Connection Lifecycle
// ===========================================================================

TEST_F(MetricsComprehensiveTest, SimulateConnectionLifecycle) {
    // Simulate a realistic QUIC connection lifecycle
    // 1. Connection open
    Metrics::GaugeInc(MetricsStd::QuicConnectionsActive);
    Metrics::CounterInc(MetricsStd::QuicConnectionsTotal);

    // 2. Handshake
    Metrics::CounterInc(MetricsStd::QuicHandshakeSuccess);
    Metrics::HistogramObserve(MetricsStd::QuicHandshakeDurationUs, 5500);

    // 3. Stream operations
    Metrics::GaugeInc(MetricsStd::QuicStreamsActive);
    Metrics::CounterInc(MetricsStd::QuicStreamsCreated);

    // 4. Data transfer
    Metrics::CounterInc(MetricsStd::QuicPacketsTx, 100);
    Metrics::CounterInc(MetricsStd::QuicPacketsRx, 95);
    Metrics::CounterInc(MetricsStd::QuicStreamsBytesTx, 50000);
    Metrics::CounterInc(MetricsStd::QuicStreamsBytesRx, 48000);
    Metrics::CounterInc(MetricsStd::QuicPacketsAcked, 90);
    Metrics::CounterInc(MetricsStd::QuicPacketsLost, 5);

    // 5. RTT updates
    Metrics::GaugeSet(MetricsStd::RttSmoothedUs, 10000);
    Metrics::GaugeSet(MetricsStd::RttMinUs, 5000);

    // 6. Congestion
    Metrics::GaugeSet(MetricsStd::CongestionWindowBytes, 131072);
    Metrics::GaugeSet(MetricsStd::BytesInFlight, 50000);

    // 7. Stream close
    Metrics::GaugeDec(MetricsStd::QuicStreamsActive);
    Metrics::CounterInc(MetricsStd::QuicStreamsClosed);

    // 8. Connection close
    Metrics::GaugeDec(MetricsStd::QuicConnectionsActive);
    Metrics::CounterInc(MetricsStd::QuicConnectionsClosed);

    std::string output = Metrics::ExportPrometheus();

    // Verify key metrics after lifecycle
    EXPECT_NE(output.find("quic_connections_active 0"), std::string::npos);
    EXPECT_NE(output.find("quic_connections_total 1"), std::string::npos);
    EXPECT_NE(output.find("quic_connections_closed 1"), std::string::npos);
    EXPECT_NE(output.find("quic_handshake_success 1"), std::string::npos);
    EXPECT_NE(output.find("quic_streams_active 0"), std::string::npos);
    EXPECT_NE(output.find("quic_packets_tx 100"), std::string::npos);
    EXPECT_NE(output.find("quic_packets_lost 5"), std::string::npos);
    EXPECT_NE(output.find("rtt_smoothed_us 10000"), std::string::npos);
}

TEST_F(MetricsComprehensiveTest, SimulateHttp3RequestLifecycle) {
    // Simulate HTTP/3 request lifecycle
    // 1. Request start
    Metrics::CounterInc(MetricsStd::Http3RequestsTotal);
    Metrics::GaugeInc(MetricsStd::Http3RequestsActive);

    // 2. Response received
    Metrics::CounterInc(MetricsStd::Http3ResponseBytesTx, 256);
    Metrics::CounterInc(MetricsStd::Http3ResponseBytesRx, 1024);
    Metrics::CounterInc(MetricsStd::Http3Responses2xx);

    // 3. Request complete
    Metrics::GaugeDec(MetricsStd::Http3RequestsActive);
    Metrics::HistogramObserve(MetricsStd::Http3RequestDurationUs, 25000);

    std::string output = Metrics::ExportPrometheus();
    EXPECT_NE(output.find("http3_requests_total 1"), std::string::npos);
    EXPECT_NE(output.find("http3_requests_active 0"), std::string::npos);
    EXPECT_NE(output.find("http3_responses_2xx 1"), std::string::npos);
    EXPECT_NE(output.find("http3_request_duration_us_count 1"), std::string::npos);
}

}  // namespace common
}  // namespace quicx
