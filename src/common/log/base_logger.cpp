#include "base_logger.h"
#include "logger_interface.h"
#include "common/util/time.h"
#include "common/alloter/normal_alloter.h"

namespace quicx {

enum LogLevelMask {
    LLM_FATAL        = 0x01,
    LLM_ERROR        = 0x02,
    LLM_WARN         = 0x04,
    LLM_INFO         = 0x08,
    LLM_DEBUG        = 0x10,
};

static void FormatLog(const char* file, uint32_t line, const char* level, const char* content, va_list list, char* buf, uint32_t& len) {
    // get time
    char time_buf[__format_time_buf_size] = {0};
    GetFormatTime(time_buf, __format_time_buf_size);

    uint32_t curlen = snprintf(buf, len, "[%s|%s|%s:%d] ", level, time_buf, file, line);
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
    FormatLog(file, line, "DEBUG", content, list, log->_log, log->_len);

    if (_logger) {
        _logger->Debug(log);
    }
}

void BaseLogger::Info(const char* file, uint32_t line, const char* content, va_list list) {
    if (!(_level & LLM_INFO)) {
        return;
    }

    std::shared_ptr<Log> log = GetLog();
    FormatLog(file, line, "INFO", content, list, log->_log, log->_len);

    if (_logger) {
        _logger->Info(log);
    }
}

void BaseLogger::Warn(const char* file, uint32_t line, const char* content, va_list list) {
    if (!(_level & LLM_WARN)) {
        return;
    }

    std::shared_ptr<Log> log = GetLog();
    FormatLog(file, line, "WARN", content, list, log->_log, log->_len);

    if (_logger) {
        _logger->Warn(log);
    }
}

void BaseLogger::Error(const char* file, uint32_t line, const char* content, va_list list) {
    if (!(_level & LLM_ERROR)) {
        return;
    }

    std::shared_ptr<Log> log = GetLog();
    FormatLog(file, line, "ERROR", content, list, log->_log, log->_len);

    if (_logger) {
        _logger->Error(log);
    }
}

void BaseLogger::Fatal(const char* file, uint32_t line, const char* content, va_list list) {
    if (!(_level & LLM_FATAL)) {
        return;
    }

    std::shared_ptr<Log> log = GetLog();
    FormatLog(file, line, "FATAL", content, list, log->_log, log->_len);

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