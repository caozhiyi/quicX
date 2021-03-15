#ifndef QUIC_COMMON_LOG_FILE_LOGGER
#define QUIC_COMMON_LOG_FILE_LOGGER

#include <mutex>
#include <fstream>

#include "logger_interface.h"
#include "thread/thread_with_queue.h"

namespace quicx {

class FileLogger: public Logger, public ThreadWithQueue<std::shared_ptr<Log>> {
public:
    FileLogger(const std::string& file = "quicx");
    ~FileLogger();

    void Run();
    void Stop();

    void Debug(std::shared_ptr<Log>& log);
    void Info(std::shared_ptr<Log>& log);
    void Warn(std::shared_ptr<Log>& log);
    void Error(std::shared_ptr<Log>& log);
    void Fatal(std::shared_ptr<Log>& log);

private:
    std::string   _file_name;
    std::fstream  _stream;
};

}

#endif