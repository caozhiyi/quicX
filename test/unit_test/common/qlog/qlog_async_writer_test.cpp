// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>

#include "common/qlog/writer/async_writer.h"
#include "common/qlog/qlog_config.h"

namespace quicx {
namespace common {
namespace {

namespace fs = std::filesystem;

// Helper: create test config with unique temp directory
QlogConfig CreateWriterTestConfig(const std::string& test_name) {
    QlogConfig config;
    config.enabled = true;
    config.output_dir = "./test_qlog_writer_" + test_name;
    config.format = QlogFileFormat::kSequential;
    config.flush_interval_ms = 10;  // fast flush for testing
    config.batch_write = true;
    return config;
}

// Helper: wait for async writer to flush (with timeout)
void WaitForFlush(AsyncWriter& writer, uint64_t expected_events, int max_wait_ms = 2000) {
    auto start = std::chrono::steady_clock::now();
    while (writer.GetTotalEventsWritten() < expected_events) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed > max_wait_ms) {
            break;
        }
    }
}

// Helper: count .qlog files in directory
size_t CountQlogFiles(const std::string& dir) {
    size_t count = 0;
    if (!fs::exists(dir)) return 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() == ".qlog") {
            count++;
        }
    }
    return count;
}

// Helper: read all content from a .qlog file
std::string ReadQlogFile(const std::string& dir, const std::string& partial_name = "") {
    if (!fs::exists(dir)) return "";
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() == ".qlog") {
            if (partial_name.empty() ||
                entry.path().filename().string().find(partial_name) != std::string::npos) {
                std::ifstream file(entry.path());
                std::stringstream ss;
                ss << file.rdbuf();
                return ss.str();
            }
        }
    }
    return "";
}

// Helper: get all .qlog file paths
std::vector<std::string> GetQlogFiles(const std::string& dir) {
    std::vector<std::string> files;
    if (!fs::exists(dir)) return files;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() == ".qlog") {
            files.push_back(entry.path().string());
        }
    }
    return files;
}

// Test fixture for AsyncWriter tests
class AsyncWriterTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {
        // Cleanup test directories
        for (const auto& dir : cleanup_dirs_) {
            if (fs::exists(dir)) {
                fs::remove_all(dir);
            }
        }
    }

    void RegisterCleanup(const std::string& dir) {
        cleanup_dirs_.push_back(dir);
    }

private:
    std::vector<std::string> cleanup_dirs_;
};

// Test: AsyncWriter creates output directory on Start()
TEST_F(AsyncWriterTest, CreatesOutputDirectory) {
    auto config = CreateWriterTestConfig("creates_dir");
    RegisterCleanup(config.output_dir);

    // Ensure directory doesn't exist
    if (fs::exists(config.output_dir)) {
        fs::remove_all(config.output_dir);
    }
    ASSERT_FALSE(fs::exists(config.output_dir));

    AsyncWriter writer(config);
    writer.Start();

    // Directory should be created
    EXPECT_TRUE(fs::exists(config.output_dir));
    EXPECT_TRUE(fs::is_directory(config.output_dir));

    writer.Stop();
}

// Test: AsyncWriter creates .qlog file when writing header
TEST_F(AsyncWriterTest, CreatesQlogFileOnWrite) {
    auto config = CreateWriterTestConfig("creates_file");
    RegisterCleanup(config.output_dir);

    AsyncWriter writer(config);
    writer.Start();

    // Write a header
    std::string header = "{\"qlog_format\":\"JSON-SEQ\",\"qlog_version\":\"0.4\"}\n";
    writer.WriteHeader("conn-001", header);

    // Write an event
    std::string event = "{\"time\":0.000,\"name\":\"quic:packet_sent\",\"data\":{}}\n";
    writer.WriteEvent("conn-001", event);

    // Wait for flush
    WaitForFlush(writer, 1);
    writer.Stop();

    // Verify file was created
    EXPECT_GE(CountQlogFiles(config.output_dir), 1u);

    // Verify file contains the data
    std::string content = ReadQlogFile(config.output_dir);
    EXPECT_FALSE(content.empty());
    EXPECT_TRUE(content.find("qlog_format") != std::string::npos);
    EXPECT_TRUE(content.find("packet_sent") != std::string::npos);
}

// Test: Multiple connections create separate files
TEST_F(AsyncWriterTest, MultipleConnectionsSeparateFiles) {
    auto config = CreateWriterTestConfig("multi_conn");
    RegisterCleanup(config.output_dir);

    AsyncWriter writer(config);
    writer.Start();

    // Write to 3 different connections
    for (int i = 0; i < 3; i++) {
        std::string conn_id = "conn-" + std::to_string(i);
        std::string header = "{\"qlog_format\":\"JSON-SEQ\",\"conn\":\"" + conn_id + "\"}\n";
        writer.WriteHeader(conn_id, header);

        std::string event = "{\"time\":0.000,\"name\":\"test\",\"data\":{\"id\":" + std::to_string(i) + "}}\n";
        writer.WriteEvent(conn_id, event);
    }

    WaitForFlush(writer, 3);
    writer.Stop();

    // Should have 3 separate .qlog files
    EXPECT_EQ(3u, CountQlogFiles(config.output_dir));
}

// Test: File naming convention includes connection ID prefix
TEST_F(AsyncWriterTest, FileNamingConvention) {
    auto config = CreateWriterTestConfig("file_naming");
    RegisterCleanup(config.output_dir);

    AsyncWriter writer(config);
    writer.Start();

    std::string conn_id = "abcdef12";
    writer.WriteHeader(conn_id, "{\"header\":true}\n");
    writer.WriteEvent(conn_id, "{\"event\":true}\n");

    WaitForFlush(writer, 1);
    writer.Stop();

    // File should contain connection ID prefix and .qlog extension
    auto files = GetQlogFiles(config.output_dir);
    ASSERT_EQ(1u, files.size());

    std::string filename = fs::path(files[0]).filename().string();
    EXPECT_TRUE(filename.find("abcdef12") != std::string::npos)
        << "Filename should contain connection ID prefix: " << filename;
    EXPECT_TRUE(filename.find(".qlog") != std::string::npos)
        << "Filename should have .qlog extension: " << filename;
}

// Test: Long connection ID is truncated to 8 characters in filename
TEST_F(AsyncWriterTest, LongConnectionIdTruncated) {
    auto config = CreateWriterTestConfig("long_cid");
    RegisterCleanup(config.output_dir);

    AsyncWriter writer(config);
    writer.Start();

    std::string long_conn_id = "abcdef0123456789abcdef";
    writer.WriteHeader(long_conn_id, "{\"header\":true}\n");
    writer.WriteEvent(long_conn_id, "{\"event\":true}\n");

    WaitForFlush(writer, 1);
    writer.Stop();

    auto files = GetQlogFiles(config.output_dir);
    ASSERT_EQ(1u, files.size());

    std::string filename = fs::path(files[0]).filename().string();
    // Should use first 8 chars
    EXPECT_TRUE(filename.find("abcdef01") != std::string::npos)
        << "Filename should contain truncated connection ID prefix: " << filename;
    // Should NOT contain the full connection ID
    EXPECT_TRUE(filename.find(long_conn_id) == std::string::npos)
        << "Filename should not contain full long connection ID: " << filename;
}

// Test: Start/Stop lifecycle
TEST_F(AsyncWriterTest, StartStopLifecycle) {
    auto config = CreateWriterTestConfig("lifecycle");
    RegisterCleanup(config.output_dir);

    AsyncWriter writer(config);

    // Stats should be 0 before start
    EXPECT_EQ(0u, writer.GetTotalEventsWritten());
    EXPECT_EQ(0u, writer.GetTotalBytesWritten());

    // Start
    writer.Start();

    // Write some data
    writer.WriteHeader("conn-1", "{\"header\":true}\n");
    writer.WriteEvent("conn-1", "{\"time\":0.000,\"name\":\"test\",\"data\":{}}\n");

    WaitForFlush(writer, 1);

    // Stats should be updated
    EXPECT_GE(writer.GetTotalEventsWritten(), 1u);
    EXPECT_GT(writer.GetTotalBytesWritten(), 0u);

    // Stop
    writer.Stop();

    // Writing after stop should be silently ignored
    uint64_t events_before = writer.GetTotalEventsWritten();
    writer.WriteEvent("conn-1", "{\"time\":1.000,\"name\":\"test2\",\"data\":{}}\n");
    // Events written should not increase (not running)
    EXPECT_EQ(events_before, writer.GetTotalEventsWritten());
}

// Test: Double start is no-op
TEST_F(AsyncWriterTest, DoubleStartNoOp) {
    auto config = CreateWriterTestConfig("double_start");
    RegisterCleanup(config.output_dir);

    AsyncWriter writer(config);
    writer.Start();
    writer.Start();  // Should be safe, no-op

    writer.WriteEvent("conn-1", "{\"event\":true}\n");
    WaitForFlush(writer, 1);

    writer.Stop();
    EXPECT_GE(writer.GetTotalEventsWritten(), 1u);
}

// Test: Double stop is no-op
TEST_F(AsyncWriterTest, DoubleStopNoOp) {
    auto config = CreateWriterTestConfig("double_stop");
    RegisterCleanup(config.output_dir);

    AsyncWriter writer(config);
    writer.Start();

    writer.WriteEvent("conn-1", "{\"event\":true}\n");
    WaitForFlush(writer, 1);

    writer.Stop();
    writer.Stop();  // Should be safe, no-op
}

// Test: Flush forces data to disk
TEST_F(AsyncWriterTest, FlushForcesDataToDisk) {
    auto config = CreateWriterTestConfig("flush_test");
    RegisterCleanup(config.output_dir);

    AsyncWriter writer(config);
    writer.Start();

    // Write header and event
    writer.WriteHeader("conn-flush", "{\"header\":true}\n");
    writer.WriteEvent("conn-flush", "{\"event\":1}\n");

    WaitForFlush(writer, 1);

    // Explicit flush
    writer.Flush();

    // Read file immediately after flush
    std::string content = ReadQlogFile(config.output_dir);
    EXPECT_TRUE(content.find("header") != std::string::npos);
    EXPECT_TRUE(content.find("event") != std::string::npos);

    writer.Stop();
}

// Test: Batch write with many events
TEST_F(AsyncWriterTest, BatchWriteMultipleEvents) {
    auto config = CreateWriterTestConfig("batch_write");
    RegisterCleanup(config.output_dir);

    AsyncWriter writer(config);
    writer.Start();

    const int num_events = 100;
    writer.WriteHeader("conn-batch", "{\"header\":true}\n");

    for (int i = 0; i < num_events; i++) {
        std::string event = "{\"time\":" + std::to_string(i) +
            ".000,\"name\":\"test\",\"data\":{\"seq\":" + std::to_string(i) + "}}\n";
        writer.WriteEvent("conn-batch", event);
    }

    WaitForFlush(writer, num_events);
    writer.Stop();

    // Verify all events were written
    EXPECT_EQ(static_cast<uint64_t>(num_events), writer.GetTotalEventsWritten());

    // Verify file content
    std::string content = ReadQlogFile(config.output_dir);
    EXPECT_FALSE(content.empty());

    // Count lines (header + events)
    int line_count = 0;
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty()) line_count++;
    }
    // Should have header line + num_events event lines
    // Note: header might be 1 or 2 lines depending on format
    EXPECT_GE(line_count, num_events);
}

// Test: Data integrity - content matches what was written
TEST_F(AsyncWriterTest, DataIntegrity) {
    auto config = CreateWriterTestConfig("data_integrity");
    RegisterCleanup(config.output_dir);

    AsyncWriter writer(config);
    writer.Start();

    std::string header = "{\"qlog_format\":\"JSON-SEQ\",\"qlog_version\":\"0.4\"}\n";
    std::string event1 = "{\"time\":100.000,\"name\":\"quic:packet_sent\",\"data\":{\"pn\":1}}\n";
    std::string event2 = "{\"time\":200.000,\"name\":\"quic:packet_received\",\"data\":{\"pn\":2}}\n";
    std::string event3 = "{\"time\":300.000,\"name\":\"recovery:metrics_updated\",\"data\":{\"cwnd\":14520}}\n";

    writer.WriteHeader("conn-integrity", header);
    writer.WriteEvent("conn-integrity", event1);
    writer.WriteEvent("conn-integrity", event2);
    writer.WriteEvent("conn-integrity", event3);

    WaitForFlush(writer, 3);
    writer.Stop();

    // Read and verify
    std::string content = ReadQlogFile(config.output_dir);
    EXPECT_TRUE(content.find("qlog_format") != std::string::npos);
    EXPECT_TRUE(content.find("packet_sent") != std::string::npos);
    EXPECT_TRUE(content.find("packet_received") != std::string::npos);
    EXPECT_TRUE(content.find("metrics_updated") != std::string::npos);
    EXPECT_TRUE(content.find("\"pn\":1") != std::string::npos);
    EXPECT_TRUE(content.find("\"pn\":2") != std::string::npos);
    EXPECT_TRUE(content.find("\"cwnd\":14520") != std::string::npos);
}

// Test: Events and bytes statistics
TEST_F(AsyncWriterTest, StatisticsAccuracy) {
    auto config = CreateWriterTestConfig("statistics");
    RegisterCleanup(config.output_dir);

    AsyncWriter writer(config);
    writer.Start();

    std::string header = "HEADER_LINE\n";
    std::string event = "EVENT_LINE_1\n";

    writer.WriteHeader("conn-stats", header);
    writer.WriteEvent("conn-stats", event);
    writer.WriteEvent("conn-stats", event);
    writer.WriteEvent("conn-stats", event);

    WaitForFlush(writer, 3);
    writer.Stop();

    // Header should not count as event
    EXPECT_EQ(3u, writer.GetTotalEventsWritten());

    // Total bytes = header + 3 events
    uint64_t expected_bytes = header.size() + event.size() * 3;
    EXPECT_EQ(expected_bytes, writer.GetTotalBytesWritten());
}

// Test: SetOutputDirectory changes where files are written
TEST_F(AsyncWriterTest, SetOutputDirectoryChangesPath) {
    auto config = CreateWriterTestConfig("change_dir");
    RegisterCleanup(config.output_dir);

    std::string new_dir = "./test_qlog_writer_new_dir";
    RegisterCleanup(new_dir);

    AsyncWriter writer(config);
    writer.Start();

    // Write to original directory
    writer.WriteHeader("conn-old", "{\"header\":true}\n");
    writer.WriteEvent("conn-old", "{\"event\":\"old\"}\n");
    WaitForFlush(writer, 1);

    // Change directory
    writer.SetOutputDirectory(new_dir);

    // Write to new directory (new connection)
    writer.WriteHeader("conn-new", "{\"header\":true}\n");
    writer.WriteEvent("conn-new", "{\"event\":\"new\"}\n");
    WaitForFlush(writer, 2);

    writer.Stop();

    // Original directory should have old connection file
    EXPECT_GE(CountQlogFiles(config.output_dir), 1u);
    // New directory should have new connection file
    EXPECT_GE(CountQlogFiles(new_dir), 1u);
}

// Test: Writing with no Start() is silently ignored
TEST_F(AsyncWriterTest, WriteBeforeStartIgnored) {
    auto config = CreateWriterTestConfig("no_start");
    RegisterCleanup(config.output_dir);

    AsyncWriter writer(config);
    // Don't call Start()

    writer.WriteHeader("conn-1", "{\"header\":true}\n");
    writer.WriteEvent("conn-1", "{\"event\":true}\n");

    // Nothing should happen
    EXPECT_EQ(0u, writer.GetTotalEventsWritten());
    EXPECT_EQ(0u, writer.GetTotalBytesWritten());
}

// Test: Stop flushes remaining queue items
TEST_F(AsyncWriterTest, StopDrainsQueue) {
    auto config = CreateWriterTestConfig("stop_drains");
    config.flush_interval_ms = 5000;  // Very long flush interval
    RegisterCleanup(config.output_dir);

    AsyncWriter writer(config);
    writer.Start();

    // Rapidly write many events
    writer.WriteHeader("conn-drain", "{\"header\":true}\n");
    for (int i = 0; i < 50; i++) {
        writer.WriteEvent("conn-drain", "{\"event\":" + std::to_string(i) + "}\n");
    }

    // Stop should drain all events
    writer.Stop();

    EXPECT_EQ(50u, writer.GetTotalEventsWritten());
}

}  // namespace
}  // namespace common
}  // namespace quicx
