// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#include <iostream>
#include "stdout_logger.h"

namespace quicx {
namespace common {

StdoutLogger::StdoutLogger() {

}

StdoutLogger::~StdoutLogger() {

}

void StdoutLogger::Debug(std::shared_ptr<Log>& log) {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        std::cout<< log->log_ << std::endl;
    }
    Logger::Debug(log);
}

void StdoutLogger::Info(std::shared_ptr<Log>& log) {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        std::cout<< log->log_ << std::endl;
    }
    Logger::Info(log);
}

void StdoutLogger::Warn(std::shared_ptr<Log>& log) {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        std::cout<< log->log_ << std::endl;
    }
    Logger::Warn(log);
}

void StdoutLogger::Error(std::shared_ptr<Log>& log) {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        std::cerr<< log->log_ << std::endl;
    }
    Logger::Error(log);
}

void StdoutLogger::Fatal(std::shared_ptr<Log>& log) {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        std::cerr<< log->log_ << std::endl;
    }
    Logger::Fatal(log);
}

}
}