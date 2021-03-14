#ifndef QUIC_COMMON_LOG_LOG_INTERFACE
#define QUIC_COMMON_LOG_LOG_INTERFACE

#include <memory>
#include <cstdint>

#include "common/util/singleton.h"
#include "common/queue/thread_safe_queue.h"

namespace quicx {

enum LogLevel {
    LL_NULL         = 0x00, // not print log
    LL_FATAL        = 0x01,
    LL_ERROR        = 0x02 | LL_FATAL,
    LL_WARN         = 0x04 | LL_ERROR,
    LL_INFO         = 0x08 | LL_WARN,
    LL_DEBUG        = 0x10 | LL_INFO,
};

// inherit this class to print log.
// can set subclasses for different print combinations.
class Log {
public:
    Log() {}
    virtual ~Log() {}

    void SetLog(std::shared_ptr<Log> log) { _log = log; }
    std::shared_ptr<Log> GetLog() { return _log; }

    virtual void Debug(const char* log, uint32_t len) = 0;
    virtual void Info(const char* log, uint32_t len) = 0;
    virtual void Warn(const char* log, uint32_t len) = 0;
    virtual void Error(const char* log, uint32_t len) = 0;
    virtual void Fatal(const char* log, uint32_t len) = 0;

protected:
    std::shared_ptr<Log> _log;
};

// basic management class of log printing
class Alloter;
class LogBase: public Singleton<LogBase> {
public:
    LogBase(uint16_t cache_queue_size = 10, uint16_t cache_size = 1024);
    ~LogBase();

    void SetLog(std::shared_ptr<Log> log) { _log = log; }
    std::shared_ptr<Log> GetLog() { return _log; }

    void SetLevel(LogLevel level) { _level = level; }
    uint16_t GetLevel() { return _level; }

    void Debug(const char* file, uint32_t line, const char* log...);
    void Info(const char* file, uint32_t line, const char* log...);
    void Warn(const char* file, uint32_t line, const char* log...);
    void Error(const char* file, uint32_t line, const char* log...);
    void Fatal(const char* file, uint32_t line, const char* log...);

    void Debug(const char* log, uint32_t len);
    void Info(const char* log, uint32_t len);
    void Warn(const char* log, uint32_t len);
    void Error(const char* log, uint32_t len);
    void Fatal(const char* log, uint32_t len);

private:
    char* GetBuf();
    void FreeBuf(char* buf);

protected:
    uint16_t _level;
    std::shared_ptr<Log> _log;

    std::shared_ptr<Alloter> _allocter;

    uint16_t _cache_queue_size;
    uint16_t _cache_block_size;
    ThreadSafeQueue<char*> _cache_queue;
};

// log interface for user
#define LOG_SET(log)         LogBase::Instance().SetLog(log);
#define LOG_SET_LEVEL(level) LogBase::Instance().SetLevel(level);

#define LOG_DEBUG(log, ...)  LogBase::Instance().Debug(__FILE__, __LINE__, log, ##__VA_ARGS__);
#define LOG_INFO(log, ...)   LogBase::Instance().Info(__FILE__, __LINE__, log, ##__VA_ARGS__);
#define LOG_WARN(log, ...)   LogBase::Instance().Warn(__FILE__, __LINE__, log, ##__VA_ARGS__);
#define LOG_ERROR(log, ...)  LogBase::Instance().Error(__FILE__, __LINE__, log, ##__VA_ARGS__);
#define LOG_FATAL(log, ...)  LogBase::Instance().Fatal(__FILE__, __LINE__, log, ##__VA_ARGS__);

}

#endif