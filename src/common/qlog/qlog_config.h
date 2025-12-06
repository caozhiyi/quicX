// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#ifndef COMMON_QLOG_QLOG_CONFIG
#define COMMON_QLOG_QLOG_CONFIG

#include <string>
#include <vector>
#include <cstdint>

namespace quicx {
namespace common {

/**
 * @brief qlog file format
 */
enum class QlogFileFormat : uint8_t {
    kContained = 0,   // Complete JSON array (requires full write)
    kSequential = 1,  // JSON Text Sequences (recommended)
};

/**
 * @brief Vantage point type
 */
enum class VantagePoint : uint8_t {
    kClient = 0,
    kServer = 1,
    kNetwork = 2,  // Future extension: network middlebox
    kUnknown = 3,
};

/**
 * @brief qlog configuration structure
 */
struct QlogConfig {
    // ========== Basic switches ==========
    bool enabled = false;
    std::string output_dir = "./qlogs";
    QlogFileFormat format = QlogFileFormat::kSequential;

    // ========== Performance optimization ==========
    uint32_t async_queue_size = 10000;   // Async queue size
    uint32_t flush_interval_ms = 100;    // Flush interval (milliseconds)
    bool batch_write = true;             // Batch write

    // ========== Event filtering ==========
    std::vector<std::string> event_whitelist;  // Whitelist (empty means log all)
    std::vector<std::string> event_blacklist;  // Blacklist
    float sampling_rate = 1.0f;          // Sampling rate (0.0-1.0)

    // ========== File management ==========
    uint64_t max_file_size_mb = 100;     // Single file size limit (MB)
    uint32_t max_file_count = 10;        // Number of files to keep
    bool auto_rotate = true;             // Auto rotation

    // ========== Privacy protection ==========
    bool log_raw_packets = false;        // Whether to log raw packets
    bool anonymize_ips = false;          // IP anonymization
    std::string time_format = "relative"; // "relative" | "absolute"
};

/**
 * @brief Common fields (reduce repetition)
 */
struct CommonFields {
    std::string protocol_type = "QUIC";
    std::string group_id;

    // Extensible for other common fields
};

/**
 * @brief qlog configuration info (recorded in Trace)
 */
struct QlogConfiguration {
    uint64_t time_offset = 0;
    std::string time_units = "us";

    // Extensible for other configurations
};

}  // namespace common
}  // namespace quicx

#endif
