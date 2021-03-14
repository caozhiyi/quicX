#include <iostream>
#include "stdout_log.h"

namespace quicx {

StdoutLog::StdoutLog() {

}

StdoutLog::~StdoutLog() {

}

void StdoutLog::Debug(const char* log, uint32_t len) {
    {
        std::unique_lock<std::mutex> lock(_mutex);
        std::cout<< log << std::endl;
    }
    if (_log) {
        _log->Debug(log, len);
    }
}

void StdoutLog::Info(const char* log, uint32_t len) {
    {
        std::unique_lock<std::mutex> lock(_mutex);
        std::cout<< log << std::endl;
    }
    if (_log) {
        _log->Info(log, len);
    }
}

void StdoutLog::Warn(const char* log, uint32_t len) {
    {
        std::unique_lock<std::mutex> lock(_mutex);
        std::cout<< log << std::endl;
    }
    if (_log) {
        _log->Warn(log, len);
    }
}

void StdoutLog::Error(const char* log, uint32_t len) {
    {
        std::unique_lock<std::mutex> lock(_mutex);
        std::cout<< log << std::endl;
    }
    if (_log) {
        _log->Error(log, len);
    }
}

void StdoutLog::Fatal(const char* log, uint32_t len) {
    {
        std::unique_lock<std::mutex> lock(_mutex);
        std::cout<< log << std::endl;
    }
    if (_log) {
        _log->Fatal(log, len);
    }
}

}