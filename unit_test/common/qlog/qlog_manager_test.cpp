// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

#include "common/qlog/qlog_manager.h"
#include "common/qlog/event/transport_events.h"

namespace quicx {
namespace common {
namespace {

class QlogManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset manager to default state
        auto& manager = QlogManager::Instance();
        QlogConfig config;
        config.enabled = false;  // Disabled by default for tests
        manager.SetConfig(config);
    }

    void TearDown() override {
        // Clean up any traces
        auto& manager = QlogManager::Instance();
        manager.Enable(false);
    }
};

// Test singleton instance
TEST_F(QlogManagerTest, SingletonInstance) {
    auto& manager1 = QlogManager::Instance();
    auto& manager2 = QlogManager::Instance();

    EXPECT_EQ(&manager1, &manager2);
}

// Test Enable/Disable
TEST_F(QlogManagerTest, EnableDisable) {
    auto& manager = QlogManager::Instance();

    EXPECT_FALSE(manager.IsEnabled());

    manager.Enable(true);
    EXPECT_TRUE(manager.IsEnabled());

    manager.Enable(false);
    EXPECT_FALSE(manager.IsEnabled());
}

// Test SetConfig
TEST_F(QlogManagerTest, SetConfig) {
    auto& manager = QlogManager::Instance();

    QlogConfig config;
    config.enabled = true;
    config.output_dir = "/tmp/qlog_test";
    config.sampling_rate = 0.5f;
    config.flush_interval_ms = 200;

    manager.SetConfig(config);

    const QlogConfig& retrieved = manager.GetConfig();
    EXPECT_TRUE(retrieved.enabled);
    EXPECT_EQ("/tmp/qlog_test", retrieved.output_dir);
    EXPECT_FLOAT_EQ(0.5f, retrieved.sampling_rate);
    EXPECT_EQ(200u, retrieved.flush_interval_ms);
}

// Test GetConfig
TEST_F(QlogManagerTest, GetConfig) {
    auto& manager = QlogManager::Instance();

    QlogConfig config;
    config.enabled = true;
    config.output_dir = "./test_qlogs";
    manager.SetConfig(config);

    const QlogConfig& retrieved = manager.GetConfig();
    EXPECT_TRUE(retrieved.enabled);
    EXPECT_EQ("./test_qlogs", retrieved.output_dir);
}

// Test SetOutputDirectory
TEST_F(QlogManagerTest, SetOutputDirectory) {
    auto& manager = QlogManager::Instance();

    manager.SetOutputDirectory("/var/log/qlog");

    const QlogConfig& config = manager.GetConfig();
    EXPECT_EQ("/var/log/qlog", config.output_dir);
}

// Test SetEventWhitelist
TEST_F(QlogManagerTest, SetEventWhitelist) {
    auto& manager = QlogManager::Instance();

    std::vector<std::string> whitelist = {
        "quic:packet_sent",
        "quic:packet_received",
        "recovery:metrics_updated"
    };

    manager.SetEventWhitelist(whitelist);

    const QlogConfig& config = manager.GetConfig();
    EXPECT_EQ(3u, config.event_whitelist.size());
    EXPECT_EQ("quic:packet_sent", config.event_whitelist[0]);
    EXPECT_EQ("quic:packet_received", config.event_whitelist[1]);
    EXPECT_EQ("recovery:metrics_updated", config.event_whitelist[2]);
}

// Test SetSamplingRate
TEST_F(QlogManagerTest, SetSamplingRate) {
    auto& manager = QlogManager::Instance();

    manager.SetSamplingRate(0.1f);
    EXPECT_FLOAT_EQ(0.1f, manager.GetConfig().sampling_rate);

    manager.SetSamplingRate(0.5f);
    EXPECT_FLOAT_EQ(0.5f, manager.GetConfig().sampling_rate);

    manager.SetSamplingRate(1.0f);
    EXPECT_FLOAT_EQ(1.0f, manager.GetConfig().sampling_rate);
}

// Test CreateTrace when disabled
TEST_F(QlogManagerTest, CreateTraceWhenDisabled) {
    auto& manager = QlogManager::Instance();
    manager.Enable(false);

    auto trace = manager.CreateTrace("conn-1", VantagePoint::kServer);

    // Should return nullptr when disabled
    EXPECT_EQ(nullptr, trace);
}

// Test CreateTrace when enabled
TEST_F(QlogManagerTest, CreateTraceWhenEnabled) {
    auto& manager = QlogManager::Instance();

    QlogConfig config;
    config.enabled = true;
    config.output_dir = "./test_qlogs";
    config.sampling_rate = 1.0f;  // Sample all connections
    manager.SetConfig(config);

    auto trace = manager.CreateTrace("conn-2", VantagePoint::kServer);

    EXPECT_NE(nullptr, trace);
    EXPECT_EQ("conn-2", trace->GetConnectionId());
    EXPECT_EQ(VantagePoint::kServer, trace->GetVantagePoint());

    manager.RemoveTrace("conn-2");
}

// Test CreateTrace with client vantage point
TEST_F(QlogManagerTest, CreateTraceClient) {
    auto& manager = QlogManager::Instance();

    QlogConfig config;
    config.enabled = true;
    config.output_dir = "./test_qlogs";
    config.sampling_rate = 1.0f;
    manager.SetConfig(config);

    auto trace = manager.CreateTrace("client-conn-1", VantagePoint::kClient);

    EXPECT_NE(nullptr, trace);
    EXPECT_EQ("client-conn-1", trace->GetConnectionId());
    EXPECT_EQ(VantagePoint::kClient, trace->GetVantagePoint());

    manager.RemoveTrace("client-conn-1");
}

// Test RemoveTrace
TEST_F(QlogManagerTest, RemoveTrace) {
    auto& manager = QlogManager::Instance();

    QlogConfig config;
    config.enabled = true;
    config.output_dir = "./test_qlogs";
    config.sampling_rate = 1.0f;
    manager.SetConfig(config);

    auto trace = manager.CreateTrace("conn-3", VantagePoint::kServer);
    EXPECT_NE(nullptr, trace);

    // Remove trace
    manager.RemoveTrace("conn-3");

    // Try to get removed trace (should return nullptr after removal)
    auto retrieved = manager.GetTrace("conn-3");
    EXPECT_EQ(nullptr, retrieved);
}

// Test GetTrace
TEST_F(QlogManagerTest, GetTrace) {
    auto& manager = QlogManager::Instance();

    QlogConfig config;
    config.enabled = true;
    config.output_dir = "./test_qlogs";
    config.sampling_rate = 1.0f;
    manager.SetConfig(config);

    auto trace = manager.CreateTrace("conn-4", VantagePoint::kServer);
    EXPECT_NE(nullptr, trace);

    auto retrieved = manager.GetTrace("conn-4");
    EXPECT_EQ(trace, retrieved);

    manager.RemoveTrace("conn-4");
}

// Test GetTrace for non-existent connection
TEST_F(QlogManagerTest, GetTraceNonExistent) {
    auto& manager = QlogManager::Instance();

    auto trace = manager.GetTrace("non-existent-conn");
    EXPECT_EQ(nullptr, trace);
}

// Test multiple traces
TEST_F(QlogManagerTest, MultipleTraces) {
    auto& manager = QlogManager::Instance();

    QlogConfig config;
    config.enabled = true;
    config.output_dir = "./test_qlogs";
    config.sampling_rate = 1.0f;
    manager.SetConfig(config);

    auto trace1 = manager.CreateTrace("conn-5", VantagePoint::kServer);
    auto trace2 = manager.CreateTrace("conn-6", VantagePoint::kClient);
    auto trace3 = manager.CreateTrace("conn-7", VantagePoint::kServer);

    EXPECT_NE(nullptr, trace1);
    EXPECT_NE(nullptr, trace2);
    EXPECT_NE(nullptr, trace3);

    EXPECT_NE(trace1, trace2);
    EXPECT_NE(trace2, trace3);
    EXPECT_NE(trace1, trace3);

    // Verify each trace has correct connection ID
    EXPECT_EQ("conn-5", trace1->GetConnectionId());
    EXPECT_EQ("conn-6", trace2->GetConnectionId());
    EXPECT_EQ("conn-7", trace3->GetConnectionId());

    // Clean up
    manager.RemoveTrace("conn-5");
    manager.RemoveTrace("conn-6");
    manager.RemoveTrace("conn-7");
}

// Test sampling with 0% rate (should not create traces)
TEST_F(QlogManagerTest, SamplingZeroPercent) {
    auto& manager = QlogManager::Instance();

    QlogConfig config;
    config.enabled = true;
    config.output_dir = "./test_qlogs";
    config.sampling_rate = 0.0f;  // Never sample
    manager.SetConfig(config);

    auto trace = manager.CreateTrace("conn-8", VantagePoint::kServer);

    // Should return nullptr when sampling rate is 0
    EXPECT_EQ(nullptr, trace);
}

// Test sampling with 100% rate (should always create traces)
TEST_F(QlogManagerTest, SamplingHundredPercent) {
    auto& manager = QlogManager::Instance();

    QlogConfig config;
    config.enabled = true;
    config.output_dir = "./test_qlogs";
    config.sampling_rate = 1.0f;  // Always sample
    manager.SetConfig(config);

    // Create multiple traces - all should succeed
    for (int i = 0; i < 10; i++) {
        std::string conn_id = "conn-" + std::to_string(100 + i);
        auto trace = manager.CreateTrace(conn_id, VantagePoint::kServer);
        EXPECT_NE(nullptr, trace);
        manager.RemoveTrace(conn_id);
    }
}

// Test Flush (should not crash)
TEST_F(QlogManagerTest, Flush) {
    auto& manager = QlogManager::Instance();

    QlogConfig config;
    config.enabled = true;
    config.output_dir = "./test_qlogs";
    config.sampling_rate = 1.0f;
    manager.SetConfig(config);

    auto trace = manager.CreateTrace("conn-9", VantagePoint::kServer);
    EXPECT_NE(nullptr, trace);

    // Log some events
    PacketSentData data;
    data.packet_number = 1;
    data.packet_type = quic::PacketType::k1RttPacketType;
    data.packet_size = 1200;
    trace->LogPacketSent(1000, data);

    // Flush should not crash
    manager.Flush();

    manager.RemoveTrace("conn-9");
}

// Test GetWriter (internal use)
TEST_F(QlogManagerTest, GetWriter) {
    auto& manager = QlogManager::Instance();

    QlogConfig config;
    config.enabled = true;
    config.output_dir = "./test_qlogs";
    manager.SetConfig(config);

    auto* writer = manager.GetWriter();
    EXPECT_NE(nullptr, writer);
}

// Test configuration persistence across enable/disable
TEST_F(QlogManagerTest, ConfigPersistenceAcrossEnableDisable) {
    auto& manager = QlogManager::Instance();

    QlogConfig config;
    config.enabled = true;
    config.output_dir = "/custom/path";
    config.sampling_rate = 0.7f;
    manager.SetConfig(config);

    // Disable
    manager.Enable(false);
    EXPECT_FALSE(manager.IsEnabled());

    // Config should still be there
    const QlogConfig& retrieved1 = manager.GetConfig();
    EXPECT_EQ("/custom/path", retrieved1.output_dir);
    EXPECT_FLOAT_EQ(0.7f, retrieved1.sampling_rate);

    // Re-enable
    manager.Enable(true);
    EXPECT_TRUE(manager.IsEnabled());

    // Config should still be preserved
    const QlogConfig& retrieved2 = manager.GetConfig();
    EXPECT_EQ("/custom/path", retrieved2.output_dir);
    EXPECT_FLOAT_EQ(0.7f, retrieved2.sampling_rate);
}

// Test duplicate connection IDs (should handle gracefully)
TEST_F(QlogManagerTest, DuplicateConnectionIds) {
    auto& manager = QlogManager::Instance();

    QlogConfig config;
    config.enabled = true;
    config.output_dir = "./test_qlogs";
    config.sampling_rate = 1.0f;
    manager.SetConfig(config);

    auto trace1 = manager.CreateTrace("conn-dup", VantagePoint::kServer);
    EXPECT_NE(nullptr, trace1);

    // Create trace with same ID - behavior depends on implementation
    auto trace2 = manager.CreateTrace("conn-dup", VantagePoint::kServer);
    // Could be same trace or different, just shouldn't crash

    manager.RemoveTrace("conn-dup");
}

// Test event whitelist applied to trace
TEST_F(QlogManagerTest, EventWhitelistAppliedToTrace) {
    auto& manager = QlogManager::Instance();

    QlogConfig config;
    config.enabled = true;
    config.output_dir = "./test_qlogs";
    config.sampling_rate = 1.0f;
    config.event_whitelist = {"quic:packet_sent"};
    manager.SetConfig(config);

    auto trace = manager.CreateTrace("conn-10", VantagePoint::kServer);
    EXPECT_NE(nullptr, trace);

    // Log packet_sent (in whitelist)
    PacketSentData sent_data;
    sent_data.packet_number = 1;
    sent_data.packet_type = quic::PacketType::k1RttPacketType;
    sent_data.packet_size = 1200;
    trace->LogPacketSent(1000, sent_data);

    // Log packet_received (not in whitelist)
    PacketReceivedData recv_data;
    recv_data.packet_number = 2;
    recv_data.packet_type = quic::PacketType::k1RttPacketType;
    recv_data.packet_size = 1200;
    trace->LogPacketReceived(2000, recv_data);

    // Only packet_sent should be counted
    EXPECT_EQ(1u, trace->GetEventCount());

    manager.RemoveTrace("conn-10");
}

// Test RemoveTrace for non-existent connection (should not crash)
TEST_F(QlogManagerTest, RemoveNonExistentTrace) {
    auto& manager = QlogManager::Instance();

    // Should not crash
    manager.RemoveTrace("non-existent-connection");
}

// Test multiple Enable calls
TEST_F(QlogManagerTest, MultipleEnableCalls) {
    auto& manager = QlogManager::Instance();

    manager.Enable(true);
    EXPECT_TRUE(manager.IsEnabled());

    manager.Enable(true);
    EXPECT_TRUE(manager.IsEnabled());

    manager.Enable(false);
    EXPECT_FALSE(manager.IsEnabled());

    manager.Enable(false);
    EXPECT_FALSE(manager.IsEnabled());
}

// Test configuration with extreme values
TEST_F(QlogManagerTest, ConfigurationExtremeValues) {
    auto& manager = QlogManager::Instance();

    QlogConfig config;
    config.enabled = true;
    config.async_queue_size = 1;  // Minimum
    config.flush_interval_ms = 0;  // Immediate
    config.max_file_size_mb = UINT64_MAX;  // Maximum
    config.sampling_rate = 1.0f;

    manager.SetConfig(config);

    const QlogConfig& retrieved = manager.GetConfig();
    EXPECT_EQ(1u, retrieved.async_queue_size);
    EXPECT_EQ(0u, retrieved.flush_interval_ms);
    EXPECT_EQ(UINT64_MAX, retrieved.max_file_size_mb);
}

}  // namespace
}  // namespace common
}  // namespace quicx
