// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "common/qlog/qlog_config.h"

namespace quicx {
namespace common {
namespace {

// Test QlogFileFormat enum
TEST(QlogConfigTest, QlogFileFormat) {
    QlogFileFormat format1 = QlogFileFormat::kContained;
    QlogFileFormat format2 = QlogFileFormat::kSequential;

    EXPECT_EQ(0, static_cast<uint8_t>(format1));
    EXPECT_EQ(1, static_cast<uint8_t>(format2));
}

// Test VantagePoint enum
TEST(QlogConfigTest, VantagePoint) {
    VantagePoint client = VantagePoint::kClient;
    VantagePoint server = VantagePoint::kServer;
    VantagePoint network = VantagePoint::kNetwork;
    VantagePoint unknown = VantagePoint::kUnknown;

    EXPECT_EQ(0, static_cast<uint8_t>(client));
    EXPECT_EQ(1, static_cast<uint8_t>(server));
    EXPECT_EQ(2, static_cast<uint8_t>(network));
    EXPECT_EQ(3, static_cast<uint8_t>(unknown));
}

// Test QlogConfig default values
TEST(QlogConfigTest, DefaultValues) {
    QlogConfig config;

    EXPECT_FALSE(config.enabled);
    EXPECT_EQ("./qlogs", config.output_dir);
    EXPECT_EQ(QlogFileFormat::kSequential, config.format);
    EXPECT_EQ(10000u, config.async_queue_size);
    EXPECT_EQ(100u, config.flush_interval_ms);
    EXPECT_TRUE(config.batch_write);
    EXPECT_TRUE(config.event_whitelist.empty());
    EXPECT_TRUE(config.event_blacklist.empty());
    EXPECT_FLOAT_EQ(1.0f, config.sampling_rate);
    EXPECT_EQ(100u, config.max_file_size_mb);
    EXPECT_EQ(10u, config.max_file_count);
    EXPECT_TRUE(config.auto_rotate);
    EXPECT_FALSE(config.log_raw_packets);
    EXPECT_FALSE(config.anonymize_ips);
    EXPECT_EQ("relative", config.time_format);
}

// Test QlogConfig custom values
TEST(QlogConfigTest, CustomValues) {
    QlogConfig config;
    config.enabled = true;
    config.output_dir = "/var/log/qlog";
    config.format = QlogFileFormat::kContained;
    config.async_queue_size = 50000;
    config.flush_interval_ms = 500;
    config.batch_write = false;
    config.event_whitelist = {"quic:packet_sent", "quic:packet_received"};
    config.event_blacklist = {"quic:packet_lost"};
    config.sampling_rate = 0.5f;
    config.max_file_size_mb = 200;
    config.max_file_count = 20;
    config.auto_rotate = false;
    config.log_raw_packets = true;
    config.anonymize_ips = true;
    config.time_format = "absolute";

    EXPECT_TRUE(config.enabled);
    EXPECT_EQ("/var/log/qlog", config.output_dir);
    EXPECT_EQ(QlogFileFormat::kContained, config.format);
    EXPECT_EQ(50000u, config.async_queue_size);
    EXPECT_EQ(500u, config.flush_interval_ms);
    EXPECT_FALSE(config.batch_write);
    EXPECT_EQ(2u, config.event_whitelist.size());
    EXPECT_EQ("quic:packet_sent", config.event_whitelist[0]);
    EXPECT_EQ("quic:packet_received", config.event_whitelist[1]);
    EXPECT_EQ(1u, config.event_blacklist.size());
    EXPECT_EQ("quic:packet_lost", config.event_blacklist[0]);
    EXPECT_FLOAT_EQ(0.5f, config.sampling_rate);
    EXPECT_EQ(200u, config.max_file_size_mb);
    EXPECT_EQ(20u, config.max_file_count);
    EXPECT_FALSE(config.auto_rotate);
    EXPECT_TRUE(config.log_raw_packets);
    EXPECT_TRUE(config.anonymize_ips);
    EXPECT_EQ("absolute", config.time_format);
}

// Test CommonFields
TEST(QlogConfigTest, CommonFieldsDefaults) {
    CommonFields fields;

    EXPECT_EQ("QUIC", fields.protocol_type);
    EXPECT_TRUE(fields.group_id.empty());
}

// Test CommonFields custom values
TEST(QlogConfigTest, CommonFieldsCustom) {
    CommonFields fields;
    fields.protocol_type = "HTTP/3";
    fields.group_id = "test-group-1";

    EXPECT_EQ("HTTP/3", fields.protocol_type);
    EXPECT_EQ("test-group-1", fields.group_id);
}

// Test QlogConfiguration defaults
TEST(QlogConfigTest, QlogConfigurationDefaults) {
    QlogConfiguration config;

    EXPECT_EQ(0u, config.time_offset);
    EXPECT_EQ("us", config.time_units);
}

// Test QlogConfiguration custom values
TEST(QlogConfigTest, QlogConfigurationCustom) {
    QlogConfiguration config;
    config.time_offset = 123456789;
    config.time_units = "ms";

    EXPECT_EQ(123456789u, config.time_offset);
    EXPECT_EQ("ms", config.time_units);
}

// Test sampling rate boundaries
TEST(QlogConfigTest, SamplingRateBoundaries) {
    QlogConfig config;

    config.sampling_rate = 0.0f;
    EXPECT_FLOAT_EQ(0.0f, config.sampling_rate);

    config.sampling_rate = 1.0f;
    EXPECT_FLOAT_EQ(1.0f, config.sampling_rate);

    config.sampling_rate = 0.1f;
    EXPECT_FLOAT_EQ(0.1f, config.sampling_rate);

    config.sampling_rate = 0.9f;
    EXPECT_FLOAT_EQ(0.9f, config.sampling_rate);
}

// Test event filter lists
TEST(QlogConfigTest, EventFilters) {
    QlogConfig config;

    // Empty filters
    EXPECT_TRUE(config.event_whitelist.empty());
    EXPECT_TRUE(config.event_blacklist.empty());

    // Add to whitelist
    config.event_whitelist.push_back("quic:packet_sent");
    config.event_whitelist.push_back("quic:packet_received");
    config.event_whitelist.push_back("recovery:metrics_updated");
    EXPECT_EQ(3u, config.event_whitelist.size());

    // Add to blacklist
    config.event_blacklist.push_back("quic:packet_lost");
    config.event_blacklist.push_back("recovery:congestion_state_updated");
    EXPECT_EQ(2u, config.event_blacklist.size());
}

// Test output directory variations
TEST(QlogConfigTest, OutputDirectoryFormats) {
    QlogConfig config;

    config.output_dir = ".";
    EXPECT_EQ(".", config.output_dir);

    config.output_dir = "./logs";
    EXPECT_EQ("./logs", config.output_dir);

    config.output_dir = "/absolute/path/to/logs";
    EXPECT_EQ("/absolute/path/to/logs", config.output_dir);

    config.output_dir = "../relative/path";
    EXPECT_EQ("../relative/path", config.output_dir);
}

// Test file size limits
TEST(QlogConfigTest, FileSizeLimits) {
    QlogConfig config;

    config.max_file_size_mb = 1;  // Minimum
    EXPECT_EQ(1u, config.max_file_size_mb);

    config.max_file_size_mb = 1000;  // Large
    EXPECT_EQ(1000u, config.max_file_size_mb);

    config.max_file_size_mb = UINT64_MAX;  // Maximum
    EXPECT_EQ(UINT64_MAX, config.max_file_size_mb);
}

// Test queue size variations
TEST(QlogConfigTest, QueueSizeVariations) {
    QlogConfig config;

    config.async_queue_size = 1000;
    EXPECT_EQ(1000u, config.async_queue_size);

    config.async_queue_size = 100000;
    EXPECT_EQ(100000u, config.async_queue_size);

    config.async_queue_size = 1;  // Minimum
    EXPECT_EQ(1u, config.async_queue_size);
}

// Test flush interval variations
TEST(QlogConfigTest, FlushIntervalVariations) {
    QlogConfig config;

    config.flush_interval_ms = 10;  // Fast
    EXPECT_EQ(10u, config.flush_interval_ms);

    config.flush_interval_ms = 1000;  // Slow
    EXPECT_EQ(1000u, config.flush_interval_ms);

    config.flush_interval_ms = 0;  // Immediate
    EXPECT_EQ(0u, config.flush_interval_ms);
}

// Test time format options
TEST(QlogConfigTest, TimeFormatOptions) {
    QlogConfig config;

    config.time_format = "relative";
    EXPECT_EQ("relative", config.time_format);

    config.time_format = "absolute";
    EXPECT_EQ("absolute", config.time_format);
}

// Test privacy settings combinations
TEST(QlogConfigTest, PrivacySettingsCombinations) {
    QlogConfig config;

    // Both disabled
    config.log_raw_packets = false;
    config.anonymize_ips = false;
    EXPECT_FALSE(config.log_raw_packets);
    EXPECT_FALSE(config.anonymize_ips);

    // Only raw packets
    config.log_raw_packets = true;
    config.anonymize_ips = false;
    EXPECT_TRUE(config.log_raw_packets);
    EXPECT_FALSE(config.anonymize_ips);

    // Only anonymize IPs
    config.log_raw_packets = false;
    config.anonymize_ips = true;
    EXPECT_FALSE(config.log_raw_packets);
    EXPECT_TRUE(config.anonymize_ips);

    // Both enabled
    config.log_raw_packets = true;
    config.anonymize_ips = true;
    EXPECT_TRUE(config.log_raw_packets);
    EXPECT_TRUE(config.anonymize_ips);
}

// Test file rotation settings
TEST(QlogConfigTest, FileRotationSettings) {
    QlogConfig config;

    // With rotation
    config.auto_rotate = true;
    config.max_file_size_mb = 50;
    config.max_file_count = 5;
    EXPECT_TRUE(config.auto_rotate);
    EXPECT_EQ(50u, config.max_file_size_mb);
    EXPECT_EQ(5u, config.max_file_count);

    // Without rotation
    config.auto_rotate = false;
    EXPECT_FALSE(config.auto_rotate);
}

}  // namespace
}  // namespace common
}  // namespace quicx
