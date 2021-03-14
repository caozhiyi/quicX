#ifndef QUIC_COMMON_LOG_STDOUT_LOG
#define QUIC_COMMON_LOG_STDOUT_LOG

#include <mutex>
#include "log_interface.h"

namespace quicx {

class StdoutLog: public Log {
public:
    StdoutLog();
    ~StdoutLog();

    void Debug(const char* log, uint32_t len);
    void Info(const char* log, uint32_t len);
    void Warn(const char* log, uint32_t len);
    void Error(const char* log, uint32_t len);
    void Fatal(const char* log, uint32_t len);

private:
    std::mutex _mutex;
};

}

#endif