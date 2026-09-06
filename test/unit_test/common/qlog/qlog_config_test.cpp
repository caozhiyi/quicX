#include <string>
#include <vector>

#include <gtest/gtest.h>

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

    EXPECT_FALSE(config.enabled_);
    EXPECT_EQ("./qlogs", config.output_dir_);
    EXPECT_EQ(QlogFileFormat::kSequential, config.format_);
    EXPECT_EQ(10000u, config.async_queue_size_);
    EXPECT_EQ(100u, config.flush_interval_ms_);
    EXPECT_TRUE(config.batch_write_);
    EXPECT_TRUE(config.event_whitelist_.empty());
    EXPECT_TRUE(config.event_blacklist_.empty());
    EXPECT_FLOAT_EQ(1.0f, config.sampling_rate_);
    EXPECT_EQ(100u, config.max_file_size_mb_);
    EXPECT_EQ(10u, config.max_file_count_);
    EXPECT_TRUE(config.auto_rotate_);
    EXPECT_FALSE(config.log_raw_packets_);
    EXPECT_FALSE(config.anonymize_ips_);
    EXPECT_EQ("relative", config.time_format_);
}

// Test QlogConfig custom values
TEST(QlogConfigTest, CustomValues) {
    QlogConfig config;
    config.enabled_ = true;
    config.output_dir_ = "/var/log/qlog";
    config.format_ = QlogFileFormat::kContained;
    config.async_queue_size_ = 50000;
    config.flush_interval_ms_ = 500;
    config.batch_write_ = false;
    config.event_whitelist_ = {"quic:packet_sent", "quic:packet_received"};
    config.event_blacklist_ = {"quic:packet_lost"};
    config.sampling_rate_ = 0.5f;
    config.max_file_size_mb_ = 200;
    config.max_file_count_ = 20;
    config.auto_rotate_ = false;
    config.log_raw_packets_ = true;
    config.anonymize_ips_ = true;
    config.time_format_ = "absolute";

    EXPECT_TRUE(config.enabled_);
    EXPECT_EQ("/var/log/qlog", config.output_dir_);
    EXPECT_EQ(QlogFileFormat::kContained, config.format_);
    EXPECT_EQ(50000u, config.async_queue_size_);
    EXPECT_EQ(500u, config.flush_interval_ms_);
    EXPECT_FALSE(config.batch_write_);
    EXPECT_EQ(2u, config.event_whitelist_.size());
    EXPECT_EQ("quic:packet_sent", config.event_whitelist_[0]);
    EXPECT_EQ("quic:packet_received", config.event_whitelist_[1]);
    EXPECT_EQ(1u, config.event_blacklist_.size());
    EXPECT_EQ("quic:packet_lost", config.event_blacklist_[0]);
    EXPECT_FLOAT_EQ(0.5f, config.sampling_rate_);
    EXPECT_EQ(200u, config.max_file_size_mb_);
    EXPECT_EQ(20u, config.max_file_count_);
    EXPECT_FALSE(config.auto_rotate_);
    EXPECT_TRUE(config.log_raw_packets_);
    EXPECT_TRUE(config.anonymize_ips_);
    EXPECT_EQ("absolute", config.time_format_);
}

// Test CommonFields
TEST(QlogConfigTest, CommonFieldsDefaults) {
    CommonFields fields;

    // Default protocol_types is the standardized identifier pair per
    // qlog draft-03.
    ASSERT_EQ(2u, fields.protocol_types.size());
    EXPECT_EQ("QUIC", fields.protocol_types[0]);
    EXPECT_EQ("HTTP3", fields.protocol_types[1]);
    EXPECT_TRUE(fields.group_id.empty());
}

// Test CommonFields custom values
TEST(QlogConfigTest, CommonFieldsCustom) {
    CommonFields fields;
    fields.protocol_types = {"HTTP/3"};
    fields.group_id = "test-group-1";

    ASSERT_EQ(1u, fields.protocol_types.size());
    EXPECT_EQ("HTTP/3", fields.protocol_types.front());
    EXPECT_EQ("test-group-1", fields.group_id);
}

// Test QlogConfiguration defaults
TEST(QlogConfigTest, QlogConfigurationDefaults) {
    QlogConfiguration config;

    EXPECT_EQ(0u, config.time_offset);
    // Default is "ms" because the serializer emits time in milliseconds.
    EXPECT_EQ("ms", config.time_units);
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

    config.sampling_rate_ = 0.0f;
    EXPECT_FLOAT_EQ(0.0f, config.sampling_rate_);

    config.sampling_rate_ = 1.0f;
    EXPECT_FLOAT_EQ(1.0f, config.sampling_rate_);

    config.sampling_rate_ = 0.1f;
    EXPECT_FLOAT_EQ(0.1f, config.sampling_rate_);

    config.sampling_rate_ = 0.9f;
    EXPECT_FLOAT_EQ(0.9f, config.sampling_rate_);
}

// Test event filter lists
TEST(QlogConfigTest, EventFilters) {
    QlogConfig config;

    // Empty filters
    EXPECT_TRUE(config.event_whitelist_.empty());
    EXPECT_TRUE(config.event_blacklist_.empty());

    // Add to whitelist
    config.event_whitelist_.push_back("quic:packet_sent");
    config.event_whitelist_.push_back("quic:packet_received");
    config.event_whitelist_.push_back("recovery:metrics_updated");
    EXPECT_EQ(3u, config.event_whitelist_.size());

    // Add to blacklist
    config.event_blacklist_.push_back("quic:packet_lost");
    config.event_blacklist_.push_back("recovery:congestion_state_updated");
    EXPECT_EQ(2u, config.event_blacklist_.size());
}

// Test output directory variations
TEST(QlogConfigTest, OutputDirectoryFormats) {
    QlogConfig config;

    config.output_dir_ = ".";
    EXPECT_EQ(".", config.output_dir_);

    config.output_dir_ = "./logs";
    EXPECT_EQ("./logs", config.output_dir_);

    config.output_dir_ = "/absolute/path/to/logs";
    EXPECT_EQ("/absolute/path/to/logs", config.output_dir_);

    config.output_dir_ = "../relative/path";
    EXPECT_EQ("../relative/path", config.output_dir_);
}

// Test file size limits
TEST(QlogConfigTest, FileSizeLimits) {
    QlogConfig config;

    config.max_file_size_mb_ = 1;  // Minimum
    EXPECT_EQ(1u, config.max_file_size_mb_);

    config.max_file_size_mb_ = 1000;  // Large
    EXPECT_EQ(1000u, config.max_file_size_mb_);

    config.max_file_size_mb_ = UINT64_MAX;  // Maximum
    EXPECT_EQ(UINT64_MAX, config.max_file_size_mb_);
}

// Test queue size variations
TEST(QlogConfigTest, QueueSizeVariations) {
    QlogConfig config;

    config.async_queue_size_ = 1000;
    EXPECT_EQ(1000u, config.async_queue_size_);

    config.async_queue_size_ = 100000;
    EXPECT_EQ(100000u, config.async_queue_size_);

    config.async_queue_size_ = 1;  // Minimum
    EXPECT_EQ(1u, config.async_queue_size_);
}

// Test flush interval variations
TEST(QlogConfigTest, FlushIntervalVariations) {
    QlogConfig config;

    config.flush_interval_ms_ = 10;  // Fast
    EXPECT_EQ(10u, config.flush_interval_ms_);

    config.flush_interval_ms_ = 1000;  // Slow
    EXPECT_EQ(1000u, config.flush_interval_ms_);

    config.flush_interval_ms_ = 0;  // Immediate
    EXPECT_EQ(0u, config.flush_interval_ms_);
}

// Test time format options
TEST(QlogConfigTest, TimeFormatOptions) {
    QlogConfig config;

    config.time_format_ = "relative";
    EXPECT_EQ("relative", config.time_format_);

    config.time_format_ = "absolute";
    EXPECT_EQ("absolute", config.time_format_);
}

// Test privacy settings combinations
TEST(QlogConfigTest, PrivacySettingsCombinations) {
    QlogConfig config;

    // Both disabled
    config.log_raw_packets_ = false;
    config.anonymize_ips_ = false;
    EXPECT_FALSE(config.log_raw_packets_);
    EXPECT_FALSE(config.anonymize_ips_);

    // Only raw packets
    config.log_raw_packets_ = true;
    config.anonymize_ips_ = false;
    EXPECT_TRUE(config.log_raw_packets_);
    EXPECT_FALSE(config.anonymize_ips_);

    // Only anonymize IPs
    config.log_raw_packets_ = false;
    config.anonymize_ips_ = true;
    EXPECT_FALSE(config.log_raw_packets_);
    EXPECT_TRUE(config.anonymize_ips_);

    // Both enabled
    config.log_raw_packets_ = true;
    config.anonymize_ips_ = true;
    EXPECT_TRUE(config.log_raw_packets_);
    EXPECT_TRUE(config.anonymize_ips_);
}

// Test file rotation settings
TEST(QlogConfigTest, FileRotationSettings) {
    QlogConfig config;

    // With rotation
    config.auto_rotate_ = true;
    config.max_file_size_mb_ = 50;
    config.max_file_count_ = 5;
    EXPECT_TRUE(config.auto_rotate_);
    EXPECT_EQ(50u, config.max_file_size_mb_);
    EXPECT_EQ(5u, config.max_file_count_);

    // Without rotation
    config.auto_rotate_ = false;
    EXPECT_FALSE(config.auto_rotate_);
}

}  // namespace
}  // namespace common
}  // namespace quicx
