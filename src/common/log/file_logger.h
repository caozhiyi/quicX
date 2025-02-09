// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_LOG_FILE_LOGGER
#define COMMON_LOG_FILE_LOGGER

#include <mutex>
#include <queue>
#include <fstream>

#include "common/log/if_logger.h"
#include "common/thread/thread_with_queue.h"

namespace quicx {
namespace common {

static const uint8_t __file_logger_time_buf_size = sizeof("xxxx-xx-xx:xx");

enum class FileLoggerSpiltUnit {
    kDay  = 1,
    kHour = 2,
};

class FileLogger: 
    public Logger, 
    public ThreadWithQueue<std::shared_ptr<Log>> {

public:
    FileLogger(const std::string& file, 
        FileLoggerSpiltUnit unit = FileLoggerSpiltUnit::kDay, 
        uint16_t max_store_days = 3,
        uint16_t time_offset = 5);

    ~FileLogger();

    void Run();
    void Stop();

    void Debug(std::shared_ptr<Log>& log);
    void Info(std::shared_ptr<Log>& log);
    void Warn(std::shared_ptr<Log>& log);
    void Error(std::shared_ptr<Log>& log);
    void Fatal(std::shared_ptr<Log>& log);

    void SetFileName(const std::string& name) { file_name_ = name; }
    std::string GetFileName() { return file_name_; }

    void SetMaxStoreDays(uint16_t max);
    uint16_t GetMAxStorDays() { return max_file_num_; }

private:
    void CheckTime(char* log);
    void CheckExpireFiles();

private:
    std::string   file_name_;
    std::fstream  stream_;

    // for time check
    uint16_t time_offset_;
    uint8_t  time_buf_len_;
    FileLoggerSpiltUnit spilt_unit_;
    char     time_[__file_logger_time_buf_size];

    // for log file delete
    uint16_t max_file_num_;
    std::queue<std::string> history_file_names_;
};

}
}

#endif