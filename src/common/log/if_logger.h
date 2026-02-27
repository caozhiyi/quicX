#ifndef COMMON_LOG_IF_LOG
#define COMMON_LOG_IF_LOG

#include <memory>
#include <cstdint>

namespace quicx {
namespace common {

/**
 * @brief Log message structure
 */
struct Log {
    char*    log_;   ///< Log message content
    uint32_t len_;   ///< Length of log message
};

/**
 * @brief Logger base class for custom logging implementations
 *
 * Inherit this class to implement custom log output destinations.
 * Supports logger chaining for multiple output targets.
 */
class Logger {
public:
    Logger() {}
    virtual ~Logger() {}

    /**
     * @brief Set the next logger in the chain
     *
     * @param logger Next logger instance
     */
    void SetLogger(std::shared_ptr<Logger> logger) { logger_ = logger; }

    /**
     * @brief Get the next logger in the chain
     *
     * @return Next logger instance
     */
    std::shared_ptr<Logger> GetLogger() { return logger_; }

    /**
     * @brief Log a debug message
     *
     * @param log Log message
     */
    virtual void Debug(std::shared_ptr<Log>& log) { if(logger_) logger_->Debug(log); }

    /**
     * @brief Log an info message
     *
     * @param log Log message
     */
    virtual void Info(std::shared_ptr<Log>& log) { if(logger_) logger_->Info(log); }

    /**
     * @brief Log a warning message
     *
     * @param log Log message
     */
    virtual void Warn(std::shared_ptr<Log>& log) { if(logger_) logger_->Warn(log); }

    /**
     * @brief Log an error message
     *
     * @param log Log message
     */
    virtual void Error(std::shared_ptr<Log>& log) { if(logger_) logger_->Error(log); }

    /**
     * @brief Log a fatal message
     *
     * @param log Log message
     */
    virtual void Fatal(std::shared_ptr<Log>& log) { if(logger_) logger_->Fatal(log); }

protected:
    std::shared_ptr<Logger> logger_;
};

}
}

#endif