#ifndef COMMON_INCLUDE_TYPE
#define COMMON_INCLUDE_TYPE

#include <cstdint>

namespace quicx {

// log level
enum class LogLevel: uint8_t {
    kNull   = 0x00, // not print log
    kFatal  = 0x01,
    kError  = 0x02 | kFatal,
    kWarn   = 0x04 | kError,
    kInfo   = 0x08 | kWarn,
    kDebug  = 0x10 | kInfo,
};

}


#endif