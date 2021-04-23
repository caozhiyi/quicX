#ifndef QUIC_COMMON_LOG_BASE_LOGGER
#define QUIC_COMMON_LOG_BASE_LOGGER

#include <memory>
#include <cstdint>
#include <stdarg.h>

#include "log.h"
#include "log_stream.h"
#include "common/structure/thread_safe_queue.h"

namespace quicx {

enum LogLevelMask {
    LLM_FATAL        = 0x01,
    LLM_ERROR        = 0x02,
    LLM_WARN         = 0x04,
    LLM_INFO         = 0x08,
    LLM_DEBUG        = 0x10,
};

// basic management class of log printing
struct Log;
class Logger;
class Alloter;
class BaseLogger {
public:
    BaseLogger(uint16_t cache_size, uint16_t block_size);
    ~BaseLogger();

    void SetLogger(std::shared_ptr<Logger> log) { _logger = log; }

    void SetLevel(LogLevel level) { _level = level; }

    void Debug(const char* file, uint32_t line, const char* content, va_list list);
    void Info(const char* file, uint32_t line, const char* content, va_list list);
    void Warn(const char* file, uint32_t line, const char* content, va_list list);
    void Error(const char* file, uint32_t line, const char* content, va_list list);
    void Fatal(const char* file, uint32_t line, const char* content, va_list list);

    LogStreamParam GetStreamParam(LogLevel level, const char* file, uint32_t line);

private:
    std::shared_ptr<Log> GetLog();
    void FreeLog(Log* log);
    Log* NewLog();

protected:
    uint16_t _level;
    uint16_t _cache_size;
    uint16_t _block_size;

    std::shared_ptr<Alloter> _allocter;
    ThreadSafeQueue<Log*>    _cache_queue;
    std::shared_ptr<Logger>  _logger;
};

}

#endif