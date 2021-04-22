#include "base_logger.h"
#include "logger_interface.h"
#include "common/util/time.h"
#include "common/alloter/normal_alloter.h"

namespace quicx {

static void FormatLog(const char* file, uint32_t line, const char* level, char* buf, uint32_t& len) {
    // format level
    uint32_t curlen = snprintf(buf, len, "[%s|", level);

    // format time
    uint32_t size = __format_time_buf_size;
    GetFormatTime(buf + curlen, size);
    curlen += size;

    // format other info
    curlen += snprintf(buf + curlen, len, "|%s:%d] ", file, line);

    len = curlen;
}

static void FormatLog(const char* file, uint32_t line, const char* level, const char* content, va_list list, char* buf, uint32_t& len) {
    // format level
    uint32_t curlen = snprintf(buf, len, "[%s|", level);

    // format time
    uint32_t size = __format_time_buf_size;
    GetFormatTime(buf + curlen, size);
    curlen += size;

    // format other info
    curlen += snprintf(buf + curlen, len, "|%s:%d] ", file, line);
    curlen += vsnprintf(buf + curlen, len - curlen, content, list);

    len = curlen;
}

BaseLogger::BaseLogger(uint16_t cache_size, uint16_t block_size):
    _level(LL_INFO),
    _cache_size(cache_size),
    _block_size(block_size) {

    _allocter = MakeNormalAlloterPtr();
    for (uint16_t i = 0; i < _cache_size; i++) {
        _cache_queue.Push(NewLog());
    }
}

BaseLogger::~BaseLogger() {
    size_t size = _cache_queue.Size();
    Log* log = nullptr;
    void* del = nullptr;
    for (size_t i = 0; i < size; i++) {
        if(_cache_queue.Pop(log)) {
            del = (void*)log;
            _allocter->Free(del);
        }
    }
}

void BaseLogger::Debug(const char* file, uint32_t line, const char* content, va_list list) {
    if (!(_level & LLM_DEBUG)) {
        return;
    }

    std::shared_ptr<Log> log = GetLog();
    FormatLog(file, line, "DEB", content, list, log->_log, log->_len);

    if (_logger) {
        _logger->Debug(log);
    }
}

void BaseLogger::Info(const char* file, uint32_t line, const char* content, va_list list) {
    if (!(_level & LLM_INFO)) {
        return;
    }

    std::shared_ptr<Log> log = GetLog();
    FormatLog(file, line, "INF", content, list, log->_log, log->_len);

    if (_logger) {
        _logger->Info(log);
    }
}

void BaseLogger::Warn(const char* file, uint32_t line, const char* content, va_list list) {
    if (!(_level & LLM_WARN)) {
        return;
    }

    std::shared_ptr<Log> log = GetLog();
    FormatLog(file, line, "WAR", content, list, log->_log, log->_len);

    if (_logger) {
        _logger->Warn(log);
    }
}

void BaseLogger::Error(const char* file, uint32_t line, const char* content, va_list list) {
    if (!(_level & LLM_ERROR)) {
        return;
    }

    std::shared_ptr<Log> log = GetLog();
    FormatLog(file, line, "ERR", content, list, log->_log, log->_len);

    if (_logger) {
        _logger->Error(log);
    }
}

void BaseLogger::Fatal(const char* file, uint32_t line, const char* content, va_list list) {
    if (!(_level & LLM_FATAL)) {
        return;
    }

    std::shared_ptr<Log> log = GetLog();
    FormatLog(file, line, "FAT", content, list, log->_log, log->_len);

    if (_logger) {
        _logger->Fatal(log);
    }
}

LogStream BaseLogger::DebugStream(const char* file, uint32_t line) {
    if (!(_level & LLM_DEBUG)) {
        return std::move(LogStream());
    }

    std::shared_ptr<Log> log = GetLog();
    FormatLog(file, line, "DEB", log->_log, log->_len);

    return std::move(LogStream(log, [this](std::shared_ptr<Log> l) { Debug(l); }));
}

LogStream BaseLogger::InfoStream(const char* file, uint32_t line) {
    if (!(_level & LLM_INFO)) {
        return std::move(LogStream());
    }

    std::shared_ptr<Log> log = GetLog();
    FormatLog(file, line, "INF", log->_log, log->_len);

    return std::move(LogStream(log, [this](std::shared_ptr<Log> l) { Info(l); }));
}

LogStream BaseLogger::WarnStream(const char* file, uint32_t line) {
    if (!(_level & LLM_WARN)) {
        return std::move(LogStream());
    }

    std::shared_ptr<Log> log = GetLog();
    FormatLog(file, line, "WAR", log->_log, log->_len);

    return std::move(LogStream(log, [this](std::shared_ptr<Log> l) { Warn(l); }));
}

LogStream BaseLogger::ErrorStream(const char* file, uint32_t line) {
    if (!(_level & LLM_ERROR)) {
        return std::move(LogStream());
    }

    std::shared_ptr<Log> log = GetLog();
    FormatLog(file, line, "ERR", log->_log, log->_len);

    return std::move(LogStream(log, [this](std::shared_ptr<Log> l) { Error(l); }));
}

LogStream BaseLogger::FatalStream(const char* file, uint32_t line) {
    if (!(_level & LLM_FATAL)) {
        return std::move(LogStream());
    }

    std::shared_ptr<Log> log = GetLog();
    FormatLog(file, line, "FAT", log->_log, log->_len);

    return std::move(LogStream(log, [this](std::shared_ptr<Log> l) { Fatal(l); }));
}

void BaseLogger::Debug(std::shared_ptr<Log> log) {
    if (_logger) {
        _logger->Debug(log);
    }
}

void BaseLogger::Info(std::shared_ptr<Log> log) {
    if (_logger) {
        _logger->Info(log);
    }
}

void BaseLogger::Warn(std::shared_ptr<Log> log) {
    if (_logger) {
        _logger->Warn(log);
    }
}

void BaseLogger::Error(std::shared_ptr<Log> log) {
    if (_logger) {
        _logger->Error(log);
    }
}

void BaseLogger::Fatal(std::shared_ptr<Log> log) {
    if (_logger) {
        _logger->Fatal(log);
    }
}

std::shared_ptr<Log> BaseLogger::GetLog() {
    Log* log = nullptr;
    if (_cache_queue.Pop(log)) {

    } else {
        log = NewLog();
    }

    return std::shared_ptr<Log>(log, [this](Log* &l) { FreeLog(l); });
}

void BaseLogger::FreeLog(Log* log) {
    if (_cache_queue.Size() > _cache_size) {
        void* del = (void*)log;
        _allocter->Free(del);

    } else {
        log->_len = _block_size;
        _cache_queue.Push(log);
    }
}

Log* BaseLogger::NewLog() {
    Log* item = (Log*)_allocter->MallocAlign(_block_size + sizeof(Log));

    item->_log = (char*)item + sizeof(Log);
    item->_len = _block_size;

    return item;
}

}