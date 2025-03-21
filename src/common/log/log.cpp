// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#include <stdarg.h>

#include "common/log/log.h"
#include "common/log/base_logger.h"

namespace quicx {
namespace common {

SingletonLogger::SingletonLogger() {
    logger_ = std::make_shared<BaseLogger>(kLogCacheSize, kLogBlockSize);
}

SingletonLogger::~SingletonLogger() {

}

void SingletonLogger::SetLogger(std::shared_ptr<Logger> log) {
    logger_->SetLogger(log);
}

void SingletonLogger::SetLevel(LogLevel level){
    logger_->SetLevel(level);
}

void SingletonLogger::Debug(const char* file, uint32_t line, const char* log...) const {
    va_list list;
    va_start(list, log);
    logger_->Debug(file, line, log, list);
    va_end(list);
}

void SingletonLogger::Info(const char* file, uint32_t line, const char* log...) const {
    va_list list;
    va_start(list, log);
    logger_->Info(file, line, log, list);
    va_end(list);
}

void SingletonLogger::Warn(const char* file, uint32_t line, const char* log...) const {
    va_list list;
    va_start(list, log);
    logger_->Warn(file, line, log, list);
    va_end(list);
}

void SingletonLogger::Error(const char* file, uint32_t line, const char* log...) const {
    va_list list;
    va_start(list, log);
    logger_->Error(file, line, log, list);
    va_end(list);
}

void SingletonLogger::Fatal(const char* file, uint32_t line, const char* log...) const {
    va_list list;
    va_start(list, log);
    logger_->Fatal(file, line, log, list);
    va_end(list);
}

LogStreamParam SingletonLogger::GetStreamParam(LogLevel level, const char* file, uint32_t line) {
    return logger_->GetStreamParam(level, file, line);
}

}
}