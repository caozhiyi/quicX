// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_LOG_LOG
#define COMMON_LOG_LOG

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

// log interface for user
#define LOG_SET(log)         SingletonLogger::Instance().SetLogger(log)
#define LOG_SET_LEVEL(level) SingletonLogger::Instance().SetLevel(level)

#define LOG_DEBUG(log, ...)  SingletonLogger::Instance().Debug(__FILE__, __LINE__, log, ##__VA_ARGS__)
#define LOG_INFO(log, ...)   SingletonLogger::Instance().Info(__FILE__, __LINE__, log, ##__VA_ARGS__)
#define LOG_WARN(log, ...)   SingletonLogger::Instance().Warn(__FILE__, __LINE__, log, ##__VA_ARGS__)
#define LOG_ERROR(log, ...)  SingletonLogger::Instance().Error(__FILE__, __LINE__, log, ##__VA_ARGS__)
#define LOG_FATAL(log, ...)  SingletonLogger::Instance().Fatal(__FILE__, __LINE__, log, ##__VA_ARGS__)

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