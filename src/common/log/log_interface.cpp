#include <stdarg.h>
#include "log_interface.h"
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

static void FormatLog(const char* file, uint32_t line, const char* level, const char* log, va_list list, char*& buf, uint32_t& len) {
    // get time
    char time_buf[__format_time_buf_size] = {0};
    GetFormatTime(time_buf, __format_time_buf_size);

    uint32_t curlen = snprintf(buf, len, "[%s|%s|%s:%d] ", level, time_buf, file, line);
    curlen += vsnprintf(buf + curlen, len - curlen, log, list);
    len = curlen;
}

LogBase::LogBase(uint16_t cache_queue_size, uint16_t cache_size):
    _level(LL_DEBUG),
    _cache_queue_size(cache_queue_size),
    _cache_block_size(cache_size) {
    _allocter = MakeNormalAlloterPtr();

    char* temp = nullptr;
    for (uint16_t i = 0; i < _cache_queue_size; i++) {
        temp = (char*)_allocter->MallocAlign(_cache_block_size);
        _cache_queue.Push(temp);
    }
}
    
LogBase::~LogBase() {
    size_t size = _cache_queue.Size();
    char* temp = nullptr;
    void* del = nullptr;
    for (size_t i = 0; i < size; i++) {
        if(_cache_queue.Pop(temp)) {
            del = (void*)temp;
            _allocter->Free(del);
        }
    }
}

void LogBase::Debug(const char* file, uint32_t line, const char* log...) {
    if (!(_level & LLM_DEBUG)) {
        return;
    }

    char* buf = GetBuf();
    uint32_t buf_len = _cache_block_size;
    va_list list;

    va_start(list, log);
    FormatLog(file, line, "DEBUG", log, list, buf, buf_len);
    va_end(list);

    Debug(buf, buf_len);

    FreeBuf(buf);
}

void LogBase::Info(const char* file, uint32_t line, const char* log...) {
    if (!(_level & LLM_INFO)) {
        return;
    }

    char* buf = GetBuf();
    uint32_t buf_len = _cache_block_size;
    va_list list;

    va_start(list, log);
    FormatLog(file, line, "INFO", log, list, buf, buf_len);
    va_end(list);

    Info(buf, buf_len);

    FreeBuf(buf);
}

void LogBase::Warn(const char* file, uint32_t line, const char* log...) {
    if (!(_level & LLM_WARN)) {
        return;
    }

    char* buf = GetBuf();
    uint32_t buf_len = _cache_block_size;
    va_list list;

    va_start(list, log);
    FormatLog(file, line, "WARN", log, list, buf, buf_len);
    va_end(list);

    Warn(buf, buf_len);

    FreeBuf(buf);
}

void LogBase::Error(const char* file, uint32_t line, const char* log...) {
    if (!(_level & LLM_ERROR)) {
        return;
    }

    char* buf = GetBuf();
    uint32_t buf_len = _cache_block_size;
    va_list list;

    va_start(list, log);
    FormatLog(file, line, "ERROR", log, list, buf, buf_len);
    va_end(list);

    Error(buf, buf_len);

    FreeBuf(buf);
}

void LogBase::Fatal(const char* file, uint32_t line, const char* log...) {
    if (!(_level & LLM_FATAL)) {
        return;
    }

    char* buf = GetBuf();
    uint32_t buf_len = _cache_block_size;
    va_list list;

    va_start(list, log);
    FormatLog(file, line, "FATAL", log, list, buf, buf_len);
    va_end(list);

    Fatal(buf, buf_len);

    FreeBuf(buf);
}


void LogBase::Debug(const char* log, uint32_t len) {
    if (_log) {
        _log->Debug(log, len);
    }
}

void LogBase::Info(const char* log, uint32_t len) {
    if (_log) {
        _log->Info(log, len);
    }
}

void LogBase::Warn(const char* log, uint32_t len) {
    if (_log) {
        _log->Warn(log, len);
    }
}

void LogBase::Error(const char* log, uint32_t len) {
    if (_log) {
        _log->Error(log, len);
    }
}

void LogBase::Fatal(const char* log, uint32_t len) {
    if (_log) {
        _log->Fatal(log, len);
    }
}

char* LogBase::GetBuf() {
    char* buf = nullptr;
    if (_cache_queue.Pop(buf)) {
        return buf;

    } else {
        buf = (char*)_allocter->MallocAlign(_cache_block_size);
    }
    return buf;
}

void LogBase::FreeBuf(char* buf) {
    if (_cache_queue.Size() > _cache_queue_size) {
        void* del = (void*)buf;
        _allocter->Free(del);

    } else {
        _cache_queue.Push(buf);
    }
}

}