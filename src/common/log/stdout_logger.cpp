#include <iostream>
#include "stdout_logger.h"

namespace quicx {

StdoutLogger::StdoutLogger() {

}

StdoutLogger::~StdoutLogger() {

}

void StdoutLogger::Debug(std::shared_ptr<Log>& log) {
    {
        std::unique_lock<std::mutex> lock(_mutex);
        std::cout<< log->_log << std::endl;
    }
    if (_logger) {
        _logger->Debug(log);
    }
}

void StdoutLogger::Info(std::shared_ptr<Log>& log) {
    {
        std::unique_lock<std::mutex> lock(_mutex);
        std::cout<< log->_log << std::endl;
    }
    if (_logger) {
        _logger->Info(log);
    }
}

void StdoutLogger::Warn(std::shared_ptr<Log>& log) {
    {
        std::unique_lock<std::mutex> lock(_mutex);
        std::cout<< log->_log << std::endl;
    }
    if (_logger) {
        _logger->Warn(log);
    }
}

void StdoutLogger::Error(std::shared_ptr<Log>& log) {
    {
        std::unique_lock<std::mutex> lock(_mutex);
        std::cerr<< log->_log << std::endl;
    }
    if (_logger) {
        _logger->Error(log);
    }
}

void StdoutLogger::Fatal(std::shared_ptr<Log>& log) {
    {
        std::unique_lock<std::mutex> lock(_mutex);
        std::cerr<< log->_log << std::endl;
    }
    if (_logger) {
        _logger->Fatal(log);
    }
}

}