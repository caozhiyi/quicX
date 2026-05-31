#ifndef COMMON_LOG_LOG
#define COMMON_LOG_LOG

#include <atomic>
#include <memory>
#include <cstdint>

#include "common/log/log_stream.h"
#include "common/util/singleton.h"

namespace quicx {
namespace common {

// log level and switch
enum LogLevel: uint8_t {
    kNull  = 0x00, // not print log
    kFatal = 0x01,
    kError = 0x02 | kFatal,
    kWarn  = 0x04 | kError,
    kInfo  = 0x08 | kWarn,
    kDebug = 0x10 | kInfo,
};

enum LogLevelMaskBit : uint8_t {
    kFatalBit = 0x01,
    kErrorBit = 0x02,
    kWarnBit  = 0x04,
    kInfoBit  = 0x08,
    kDebugBit = 0x10,
};

// global log level for fast short-circuit in macros (avoid argument evaluation
// like building std::string when the log will be discarded anyway).
extern std::atomic<uint8_t> g_log_level;

inline bool LogLevelEnabled(uint8_t bit) {
    return (g_log_level.load(std::memory_order_relaxed) & bit) != 0;
}

// log interface for user
#define LOG_SET(log)         ::quicx::common::SingletonLogger::Instance().SetLogger(log)
#define LOG_SET_LEVEL(level) ::quicx::common::SingletonLogger::Instance().SetLevel(level)

// NOTE: level short-circuit at the macro layer prevents evaluation of
// arguments (e.g. FrameType2String(...).c_str()) when the level is disabled,
// which is important for hot paths.
#define LOG_DEBUG(log, ...)  do { if (::quicx::common::LogLevelEnabled(::quicx::common::kDebugBit)) \
    ::quicx::common::SingletonLogger::Instance().Debug(__FILE__, __LINE__, log, ##__VA_ARGS__); } while (0)
#define LOG_INFO(log, ...)   do { if (::quicx::common::LogLevelEnabled(::quicx::common::kInfoBit))  \
    ::quicx::common::SingletonLogger::Instance().Info (__FILE__, __LINE__, log, ##__VA_ARGS__); } while (0)
#define LOG_WARN(log, ...)   do { if (::quicx::common::LogLevelEnabled(::quicx::common::kWarnBit))  \
    ::quicx::common::SingletonLogger::Instance().Warn (__FILE__, __LINE__, log, ##__VA_ARGS__); } while (0)
#define LOG_ERROR(log, ...)  do { if (::quicx::common::LogLevelEnabled(::quicx::common::kErrorBit)) \
    ::quicx::common::SingletonLogger::Instance().Error(__FILE__, __LINE__, log, ##__VA_ARGS__); } while (0)
#define LOG_FATAL(log, ...)  do { if (::quicx::common::LogLevelEnabled(::quicx::common::kFatalBit)) \
    ::quicx::common::SingletonLogger::Instance().Fatal(__FILE__, __LINE__, log, ##__VA_ARGS__); } while (0)

#define LOG_DEBUG_S LogStream(SingletonLogger::Instance().GetStreamParam(LogLevel::kDebug, __FILE__, __LINE__))
#define LOG_INFO_S  LogStream(SingletonLogger::Instance().GetStreamParam(LogLevel::kInfo, __FILE__, __LINE__))
#define LOG_WARN_S  LogStream(SingletonLogger::Instance().GetStreamParam(LogLevel::kWarn, __FILE__, __LINE__))
#define LOG_ERROR_S LogStream(SingletonLogger::Instance().GetStreamParam(LogLevel::kError, __FILE__, __LINE__))
#define LOG_FATAL_S LogStream(SingletonLogger::Instance().GetStreamParam(LogLevel::kFatal, __FILE__, __LINE__))

// log cache config
static const uint16_t kLogCacheSize = 20;
static const uint16_t kLogBlockSize = 2048;


class Logger;
class BaseLogger;
class SingletonLogger: 
    public common::Singleton<SingletonLogger> {

public:
    SingletonLogger();
    ~SingletonLogger();

    void SetLogger(std::shared_ptr<Logger> log);

    void SetLevel(LogLevel level);

    // for log print as printf
    void Debug(const char* file, uint32_t line, const char* log...) const;
    void Info(const char* file, uint32_t line, const char* log...) const;
    void Warn(const char* file, uint32_t line, const char* log...) const;
    void Error(const char* file, uint32_t line, const char* log...) const;
    void Fatal(const char* file, uint32_t line, const char* log...) const;

    // for log stream
    LogStreamParam GetStreamParam(LogLevel level, const char* file, uint32_t line);

private:
    std::shared_ptr<BaseLogger> logger_;
};

}
}

#endif