// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_LOG_BASE_LOGGER
#define COMMON_LOG_BASE_LOGGER

#include <memory>
#include <cstdint>
#include <stdarg.h>

#include "common/log/log.h"
#include "common/log/log_stream.h"
#include "common/structure/thread_safe_queue.h"

namespace quicx {
namespace common {

// basic management class of log printing
struct Log;
class Logger;
class IAlloter;
class BaseLogger {
public:
    BaseLogger(uint16_t cache_size, uint16_t block_size);
    ~BaseLogger();

    void SetLogger(std::shared_ptr<Logger> log) { logger_ = log; }

    void SetLevel(LogLevel level);

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
    uint16_t level_;
    uint16_t cache_size_;
    uint16_t block_size_;

    std::shared_ptr<IAlloter> allocter_;
    ThreadSafeQueue<Log*>    cache_queue_;
    std::shared_ptr<Logger>  logger_;
};

}
}

#endif