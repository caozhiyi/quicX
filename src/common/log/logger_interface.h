#ifndef QUIC_COMMON_LOG_LOG_INTERFACE
#define QUIC_COMMON_LOG_LOG_INTERFACE

#include <memory>
#include <cstdint>

namespace quicx {

struct Log {
    char*    _log;
    uint32_t _len;
};

// inherit this class to print log.
// can set subclasses for different print combinations.
class Logger {
public:
    Logger() {}
    virtual ~Logger() {}

    void SetLogger(std::shared_ptr<Logger> logger) { _logger = logger; }
    std::shared_ptr<Logger> GetLogger() { return _logger; }

    virtual void Debug(std::shared_ptr<Log>& log) { if(_logger) _logger->Debug(log); }
    virtual void Info(std::shared_ptr<Log>& log) { if(_logger) _logger->Info(log); }
    virtual void Warn(std::shared_ptr<Log>& log) { if(_logger) _logger->Warn(log); }
    virtual void Error(std::shared_ptr<Log>& log) { if(_logger) _logger->Error(log); }
    virtual void Fatal(std::shared_ptr<Log>& log) { if(_logger) _logger->Fatal(log); }

protected:
    std::shared_ptr<Logger> _logger;
};

}

#endif