#ifndef COMMON_INCLUDE_TYPE
#define COMMON_INCLUDE_TYPE

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace quicx {

// log level
enum class LogLevel : uint8_t {
    kNull = 0x00,  // not print log
    kFatal = 0x01,
    kError = 0x02 | kFatal,
    kWarn = 0x04 | kError,
    kInfo = 0x08 | kWarn,
    kDebug = 0x10 | kInfo,
};

struct MetricsConfig {
    bool enable = true;            // Enable metrics collection system-wide
    size_t initial_slots = 1024;   // Initial pre-allocated slots for metrics
    std::string prefix = "quicx";  // Metrics name prefix (e.g. "quicx_packets_sent")

    // HTTP/3 Endpoint Configuration
    bool http_enable = false;            // Enable built-in HTTP/3 metrics endpoint
    uint16_t http_port = 8828;           // Port for standalone metrics server (if implemented)
    std::string http_path = "/metrics";  // Path for metrics endpoint (e.g. "https://host/metrics")
};

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
 * @brief QLog configuration
 */
struct QlogConfig {
    bool enabled = false;
    std::string output_dir = "./qlogs";
    QlogFileFormat format = QlogFileFormat::kSequential;

    // Performance optimization
    uint32_t async_queue_size = 10000;  // Async queue size
    uint32_t flush_interval_ms = 100;   // Flush interval (milliseconds)
    bool batch_write = true;            // Batch write

    // Event filtering
    std::vector<std::string> event_whitelist;  // Whitelist (empty means log all)
    std::vector<std::string> event_blacklist;  // Blacklist
    float sampling_rate = 1.0f;                // Sampling rate (0.0-1.0)

    // File management
    uint64_t max_file_size_mb = 100;  // Single file size limit (MB)
    uint32_t max_file_count = 10;     // Number of files to keep
    bool auto_rotate = true;          // Auto rotation

    // Privacy protection
    bool log_raw_packets = false;          // Whether to log raw packets
    bool anonymize_ips = false;            // IP anonymization
    std::string time_format = "relative";  // "relative" | "absolute"
};

}  // namespace quicx

#endif