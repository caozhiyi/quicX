#ifndef COMMON_INCLUDE_TYPE
#define COMMON_INCLUDE_TYPE

#include <cstddef>
#include <cstdint>
#include <string>

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
    bool enable = true;            // Enable metrics collection
    size_t initial_slots = 1024;   // Initial pre-allocated slots for metrics
    std::string prefix = "quicx";  // Metrics name prefix
};

}  // namespace quicx

#endif