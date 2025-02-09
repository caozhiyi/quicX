// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#include "common/util/time.h"
#include "common/log/if_logger.h"
#include "common/log/base_logger.h"
#include "common/alloter/normal_alloter.h"

namespace quicx {
namespace common {

enum LogLevelMask {
    kFatalMask  = 0x01,
    kErrorMask  = 0x02,
    kWarnMask   = 0x04,
    kInfoMask   = 0x08,
    kDebugMask  = 0x10,
};

static uint32_t FormatLog(const char* file, uint32_t line, const char* level, char* buf, uint32_t len) {
    // format level
    uint32_t curlen = snprintf(buf, len, "[%s|", level);

    // format time
    uint32_t size = kFormatTimeBufSize;
    GetFormatTime(buf + curlen, size);
    curlen += size;

    // format other info
    curlen += snprintf(buf + curlen, len - curlen, "|%s:%d] ", file, line);

    return curlen;
}

static uint32_t FormatLog(const char* file, uint32_t line, const char* level, const char* content, va_list list, char* buf, uint32_t len) {
    uint32_t curlen = 0;

    // format level time and file name
    curlen += FormatLog(file, line, level, buf, len);

    curlen += vsnprintf(buf + curlen, len - curlen, content, list);

    return curlen;
}

BaseLogger::BaseLogger(uint16_t cache_size, uint16_t block_size):
    level_(LogLevel::kInfo),
    cache_size_(cache_size),
    block_size_(block_size) {

    allocter_ = MakeNormalAlloterPtr();
}

BaseLogger::~BaseLogger() {
    SetLevel(LogLevel::kNull);
}

void BaseLogger::SetLevel(LogLevel level) { 
    level_ = level; 
    if (level_ > LogLevel::kNull && cache_queue_.Empty()) {
        for (uint16_t i = 0; i < cache_size_; i++) {
            cache_queue_.Push(NewLog());
        }

    } else if (level_ == LogLevel::kNull) {
        size_t size = cache_queue_.Size();
        Log* log = nullptr;
        void* del = nullptr;
        for (size_t i = 0; i < size; i++) {
            if (cache_queue_.Pop(log)) {
                del = (void*)log;
                allocter_->Free(del);
            }
        }
    }
}

void BaseLogger::Debug(const char* file, uint32_t line, const char* content, va_list list) {
    if (!(level_ & kDebugMask)) {
        return;
    }

    std::shared_ptr<Log> log = GetLog();
    log->len_ = FormatLog(file, line, "DEB", content, list, log->log_, log->len_);

    if (logger_) {
        logger_->Debug(log);
    }
}

void BaseLogger::Info(const char* file, uint32_t line, const char* content, va_list list) {
    if (!(level_ & kInfoMask)) {
        return;
    }

    std::shared_ptr<Log> log = GetLog();
    log->len_ = FormatLog(file, line, "INF", content, list, log->log_, log->len_);

    if (logger_) {
        logger_->Info(log);
    }
}

void BaseLogger::Warn(const char* file, uint32_t line, const char* content, va_list list) {
    if (!(level_ & kWarnMask)) {
        return;
    }

    std::shared_ptr<Log> log = GetLog();
    log->len_ = FormatLog(file, line, "WAR", content, list, log->log_, log->len_);

    if (logger_) {
        logger_->Warn(log);
    }
}

void BaseLogger::Error(const char* file, uint32_t line, const char* content, va_list list) {
    if (!(level_ & kErrorMask)) {
        return;
    }

    std::shared_ptr<Log> log = GetLog();
    log->len_ = FormatLog(file, line, "ERR", content, list, log->log_, log->len_);

    if (logger_) {
        logger_->Error(log);
    }
}

void BaseLogger::Fatal(const char* file, uint32_t line, const char* content, va_list list) {
    if (!(level_ & kFatalMask)) {
        return;
    }

    std::shared_ptr<Log> log = GetLog();
    log->len_ = FormatLog(file, line, "FAT", content, list, log->log_, log->len_);

    if (logger_) {
        logger_->Fatal(log);
    }
}

LogStreamParam BaseLogger::GetStreamParam(LogLevel level, const char* file, uint32_t line) {
    // check log level can print
    if (level > level_) {
        return std::make_pair(nullptr, nullptr);
    }

    std::shared_ptr<Log> log = GetLog();
    std::function<void(std::shared_ptr<Log>)> cb;
    switch (level)
    {
    case LogLevel::kNull:
        break;
    case LogLevel::kFatal:
        cb = [this](std::shared_ptr<Log> l) { logger_->Fatal(l); };
        log->len_ = FormatLog(file, line, "FAT", log->log_, log->len_);
        break;
    case LogLevel::kError:
        cb = [this](std::shared_ptr<Log> l) { logger_->Error(l); };
        log->len_ = FormatLog(file, line, "ERR", log->log_, log->len_);
        break;
    case LogLevel::kWarn:
        cb = [this](std::shared_ptr<Log> l) { logger_->Warn(l); };
        log->len_ = FormatLog(file, line, "WAR", log->log_, log->len_);
        break;
    case LogLevel::kInfo:
        cb = [this](std::shared_ptr<Log> l) { logger_->Info(l); };
        log->len_ = FormatLog(file, line, "INF", log->log_, log->len_);
        break;
    case LogLevel::kDebug:
        cb = [this](std::shared_ptr<Log> l) { logger_->Debug(l); };
        log->len_ = FormatLog(file, line, "DEB", log->log_, log->len_);
        break;
    default:
        return std::make_pair(nullptr, nullptr);
    }

    return std::make_pair(log, cb);
}

std::shared_ptr<Log> BaseLogger::GetLog() {
    Log* log = nullptr;
    if (cache_queue_.Pop(log)) {

    } else {
        log = NewLog();
    }

    return std::shared_ptr<Log>(log, [this](Log* l) { FreeLog(l); });
}

void BaseLogger::FreeLog(Log* log) {
    if (cache_queue_.Size() > cache_size_) {
        void* del = (void*)log;
        allocter_->Free(del);

    } else {
        log->len_ = block_size_;
        cache_queue_.Push(log);
    }
}

Log* BaseLogger::NewLog() {
    Log* item = (Log*)allocter_->MallocAlign(block_size_ + sizeof(Log));

    item->log_ = (char*)item + sizeof(Log);
    item->len_ = block_size_;

    return item;
}

}
}