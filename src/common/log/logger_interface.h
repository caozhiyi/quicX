// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef QUIC_COMMON_LOG_LOG_INTERFACE
#define QUIC_COMMON_LOG_LOG_INTERFACE

#include <memory>
#include <cstdint>

namespace quicx {
namespace common {

struct Log {
    char*    log_;
    uint32_t len_;
};

// inherit this class to print log.
// can set subclasses for different print combinations.
class Logger {
public:
    Logger() {}
    virtual ~Logger() {}

    void SetLogger(std::shared_ptr<Logger> logger) { logger_ = logger; }
    std::shared_ptr<Logger> GetLogger() { return logger_; }

    virtual void Debug(std::shared_ptr<Log>& log) { if(logger_) logger_->Debug(log); }
    virtual void Info(std::shared_ptr<Log>& log) { if(logger_) logger_->Info(log); }
    virtual void Warn(std::shared_ptr<Log>& log) { if(logger_) logger_->Warn(log); }
    virtual void Error(std::shared_ptr<Log>& log) { if(logger_) logger_->Error(log); }
    virtual void Fatal(std::shared_ptr<Log>& log) { if(logger_) logger_->Fatal(log); }

protected:
    std::shared_ptr<Logger> logger_;
};

}
}

#endif