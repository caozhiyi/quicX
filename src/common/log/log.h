#ifndef QUIC_COMMON_LOG_LOG
#define QUIC_COMMON_LOG_LOG

#include <memory>
#include <cstdint>

#include "log_stream.h"
#include "common/util/singleton.h"

namespace quicx {

// log level and switch
enum LogLevel {
    LL_NULL         = 0x00, // not print log
    LL_FATAL        = 0x01,
    LL_ERROR        = 0x02 | LL_FATAL,
    LL_WARN         = 0x04 | LL_ERROR,
    LL_INFO         = 0x08 | LL_WARN,
    LL_DEBUG        = 0x10 | LL_INFO,
};

// log interface for user
#define LOG_SET(log)         SingletonLogger::Instance().SetLogger(log);
#define LOG_SET_LEVEL(level) SingletonLogger::Instance().SetLevel(level);

#define LOG_DEBUG(log, ...)  SingletonLogger::Instance().Debug(__FILE__, __LINE__, log, ##__VA_ARGS__);
#define LOG_INFO(log, ...)   SingletonLogger::Instance().Info(__FILE__, __LINE__, log, ##__VA_ARGS__);
#define LOG_WARN(log, ...)   SingletonLogger::Instance().Warn(__FILE__, __LINE__, log, ##__VA_ARGS__);
#define LOG_ERROR(log, ...)  SingletonLogger::Instance().Error(__FILE__, __LINE__, log, ##__VA_ARGS__);
#define LOG_FATAL(log, ...)  SingletonLogger::Instance().Fatal(__FILE__, __LINE__, log, ##__VA_ARGS__);

#define LOG_DEBUG_S  SingletonLogger::Instance().DebugStream(__FILE__, __LINE__)
#define LOG_INFO_S   SingletonLogger::Instance().InfoStream(__FILE__, __LINE__)
#define LOG_WARN_S   SingletonLogger::Instance().WarnStream(__FILE__, __LINE__)
#define LOG_ERROR_S  SingletonLogger::Instance().ErrorStream(__FILE__, __LINE__)
#define LOG_FATAL_S  SingletonLogger::Instance().FatalStream(__FILE__, __LINE__)

// log cache config
static const uint16_t __log_cache_size = 20;
static const uint16_t __log_block_size = 1024; 


class Logger;
class BaseLogger;
class SingletonLogger: public Singleton<SingletonLogger> {
public:
    SingletonLogger();
    ~SingletonLogger();

    void SetLogger(std::shared_ptr<Logger> log);

    void SetLevel(LogLevel level);

    void Debug(const char* file, uint32_t line, const char* log...);
    void Info(const char* file, uint32_t line, const char* log...);
    void Warn(const char* file, uint32_t line, const char* log...);
    void Error(const char* file, uint32_t line, const char* log...);
    void Fatal(const char* file, uint32_t line, const char* log...);

    LogStream DebugStream(const char* file, uint32_t line);
    LogStream InfoStream(const char* file, uint32_t line);
    LogStream WarnStream(const char* file, uint32_t line);
    LogStream ErrorStream(const char* file, uint32_t line);
    LogStream FatalStream(const char* file, uint32_t line);

private:
    std::shared_ptr<BaseLogger> _logger;
};

}

#endif