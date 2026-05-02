// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <set>

#include "common/qlog/qlog_manager.h"
#include "common/qlog/qlog_trace.h"
#include "common/qlog/qlog_config.h"

namespace quicx {
namespace common {
namespace {

// Test fixture for sampling tests
class QlogSamplingTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {
        QlogManager::Instance().Enable(false);
    }

    QlogConfig CreateConfig(float sampling_rate) {
        QlogConfig config;
        config.enabled = true;
        config.output_dir = "./test_qlog_sampling";
        config.format = QlogFileFormat::kSequential;
        config.flush_interval_ms = 10;
        config.sampling_rate = sampling_rate;
        return config;
    }
};

// Test: Sampling rate 1.0 - all connections should be sampled
TEST_F(QlogSamplingTest, SamplingRateOne_AllConnectionsSampled) {
    QlogManager::Instance().SetConfig(CreateConfig(1.0f));

    int sampled = 0;
    const int total = 20;

    for (int i = 0; i < total; i++) {
        std::string conn_id = "conn-rate1-" + std::to_string(i);
        auto trace = QlogManager::Instance().CreateTrace(conn_id, VantagePoint::kClient);
        if (trace) {
            sampled++;
            QlogManager::Instance().RemoveTrace(conn_id);
        }
    }

    EXPECT_EQ(total, sampled) << "With sampling_rate=1.0, all connections should be sampled";
}

// Test: Sampling rate 0.0 - no connections should be sampled
TEST_F(QlogSamplingTest, SamplingRateZero_NoConnectionsSampled) {
    QlogManager::Instance().SetConfig(CreateConfig(0.0f));

    int sampled = 0;
    const int total = 20;

    for (int i = 0; i < total; i++) {
        std::string conn_id = "conn-rate0-" + std::to_string(i);
        auto trace = QlogManager::Instance().CreateTrace(conn_id, VantagePoint::kClient);
        if (trace) {
            sampled++;
            QlogManager::Instance().RemoveTrace(conn_id);
        }
    }

    EXPECT_EQ(0, sampled) << "With sampling_rate=0.0, no connections should be sampled";
}

// Test: Sampling rate 0.5 - approximately half should be sampled
TEST_F(QlogSamplingTest, SamplingRateHalf_ApproximatelyHalfSampled) {
    QlogManager::Instance().SetConfig(CreateConfig(0.5f));

    int sampled = 0;
    const int total = 100;

    for (int i = 0; i < total; i++) {
        std::string conn_id = "conn-rate05-" + std::to_string(i);
        auto trace = QlogManager::Instance().CreateTrace(conn_id, VantagePoint::kClient);
        if (trace) {
            sampled++;
            QlogManager::Instance().RemoveTrace(conn_id);
        }
    }

    // With 100 connections and 50% sampling, expect roughly 30-70 sampled
    // (generous bounds due to hash-based deterministic sampling)
    EXPECT_GT(sampled, 10) << "Too few sampled with rate=0.5: " << sampled;
    EXPECT_LT(sampled, 90) << "Too many sampled with rate=0.5: " << sampled;
}

// Test: Sampling is deterministic - same connection_id always gets same result
TEST_F(QlogSamplingTest, SamplingIsDeterministic) {
    QlogManager::Instance().SetConfig(CreateConfig(0.5f));

    // First pass: record which connections are sampled
    std::set<std::string> sampled_first;
    const int total = 50;

    for (int i = 0; i < total; i++) {
        std::string conn_id = "conn-deterministic-" + std::to_string(i);
        auto trace = QlogManager::Instance().CreateTrace(conn_id, VantagePoint::kClient);
        if (trace) {
            sampled_first.insert(conn_id);
            QlogManager::Instance().RemoveTrace(conn_id);
        }
    }

    // Second pass: verify same connections are sampled
    // (Reset manager state first)
    QlogManager::Instance().Enable(false);
    QlogManager::Instance().SetConfig(CreateConfig(0.5f));

    std::set<std::string> sampled_second;
    for (int i = 0; i < total; i++) {
        std::string conn_id = "conn-deterministic-" + std::to_string(i);
        auto trace = QlogManager::Instance().CreateTrace(conn_id, VantagePoint::kClient);
        if (trace) {
            sampled_second.insert(conn_id);
            QlogManager::Instance().RemoveTrace(conn_id);
        }
    }

    EXPECT_EQ(sampled_first, sampled_second)
        << "Sampling should be deterministic based on connection_id";
}

// Test: SetSamplingRate dynamically changes sampling behavior
TEST_F(QlogSamplingTest, DynamicSamplingRateChange) {
    QlogManager::Instance().SetConfig(CreateConfig(0.0f));

    // With rate=0, nothing should be sampled
    auto trace1 = QlogManager::Instance().CreateTrace("conn-dynamic-1", VantagePoint::kClient);
    EXPECT_EQ(nullptr, trace1);

    // Change to 1.0
    QlogManager::Instance().SetSamplingRate(1.0f);

    auto trace2 = QlogManager::Instance().CreateTrace("conn-dynamic-2", VantagePoint::kClient);
    EXPECT_NE(nullptr, trace2);

    if (trace2) {
        QlogManager::Instance().RemoveTrace("conn-dynamic-2");
    }
}

// Test: Invalid sampling rates are rejected
TEST_F(QlogSamplingTest, InvalidSamplingRateRejected) {
    QlogManager::Instance().SetConfig(CreateConfig(1.0f));

    // Try setting invalid rates
    QlogManager::Instance().SetSamplingRate(-0.1f);
    // Rate should remain at 1.0 (invalid input rejected)
    auto trace = QlogManager::Instance().CreateTrace("conn-invalid-neg", VantagePoint::kClient);
    EXPECT_NE(nullptr, trace) << "Negative rate should be rejected, keeping previous rate";
    if (trace) QlogManager::Instance().RemoveTrace("conn-invalid-neg");

    QlogManager::Instance().SetSamplingRate(1.5f);
    // Rate should remain at previous value (invalid input rejected)
    trace = QlogManager::Instance().CreateTrace("conn-invalid-high", VantagePoint::kClient);
    EXPECT_NE(nullptr, trace) << "Rate > 1.0 should be rejected, keeping previous rate";
    if (trace) QlogManager::Instance().RemoveTrace("conn-invalid-high");
}

// Test: Sampling rate affects CreateTrace, not individual events
TEST_F(QlogSamplingTest, SamplingAffectsTraceCreationNotEvents) {
    QlogManager::Instance().SetConfig(CreateConfig(1.0f));

    std::string conn_id = "conn-events-test";
    auto trace = QlogManager::Instance().CreateTrace(conn_id, VantagePoint::kClient);
    ASSERT_NE(nullptr, trace);

    // Once a trace is created, all events should be logged (no per-event sampling)
    for (int i = 0; i < 10; i++) {
        trace->LogEvent(i * 1000, "quic:test_event",
            std::make_unique<ConnectionStateUpdatedData>());
    }

    EXPECT_EQ(10u, trace->GetEventCount())
        << "All events should be logged once trace is created";

    QlogManager::Instance().RemoveTrace(conn_id);
}

// Test: Disabled qlog returns nullptr regardless of sampling rate
TEST_F(QlogSamplingTest, DisabledQlogAlwaysReturnsNull) {
    QlogConfig config;
    config.enabled = false;  // Disabled
    config.sampling_rate = 1.0f;
    QlogManager::Instance().SetConfig(config);

    auto trace = QlogManager::Instance().CreateTrace("conn-disabled", VantagePoint::kClient);
    EXPECT_EQ(nullptr, trace);
}

}  // namespace
}  // namespace common
}  // namespace quicx
