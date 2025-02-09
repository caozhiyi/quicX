// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#include <cstdio>
#include <cstring>

#include "common/util/time.h"
#include "common/log/file_logger.h"

namespace quicx {
namespace common {

FileLogger::FileLogger(const std::string& file, 
    FileLoggerSpiltUnit unit, 
    uint16_t max_store_days,
    uint16_t time_offset):
    file_name_(file),
    time_offset_(time_offset),
    spilt_unit_(unit) {

    if (unit == FileLoggerSpiltUnit::kHour) {
        time_buf_len_ = 13; // xxxx-xx-xx:xx
        max_file_num_ = max_store_days * 24;

    } else {
        time_buf_len_ = 10; // xxxx-xx-xx
        max_file_num_ = max_store_days;
    }

    memset(time_, 0, __file_logger_time_buf_size);

    Start();
}

FileLogger::~FileLogger() {
    Stop();
    Join();
    stream_.close();
}

void FileLogger::Run() {
     while (!stop_) {
        auto log = Pop();
        if (log) {
            CheckTime(log->log_);
            if (stream_.is_open()) {
                stream_.write(log->log_, log->len_);
                stream_.put('\n');
                stream_.flush();
            }

        } else {
            break;
        }
    }
}

void FileLogger::Stop() {
    stop_ = true;
    Push(nullptr);
}

void FileLogger::Debug(std::shared_ptr<Log>& log) {
    Push(log);
    Logger::Debug(log);
}

void FileLogger::Info(std::shared_ptr<Log>& log) {
    Push(log);
    Logger::Info(log);
}

void FileLogger::Warn(std::shared_ptr<Log>& log) {
    Push(log);
    Logger::Warn(log);
}

void FileLogger::Error(std::shared_ptr<Log>& log) {
    Push(log);
    Logger::Error(log);
}

void FileLogger::Fatal(std::shared_ptr<Log>& log) {
    Push(log);
    Logger::Fatal(log);
}

void FileLogger::SetMaxStoreDays(uint16_t max) {
    if (spilt_unit_ == FileLoggerSpiltUnit::kHour) {
        time_buf_len_ = 13; // xxxx-xx-xx:xx
        max_file_num_ = max * 24;

    } else {
        time_buf_len_ = 10; // xxxx-xx-xx
        max_file_num_ = max;
    }

    CheckExpireFiles();
}

void FileLogger::CheckTime(char* log) {
    if (strncmp(time_, log + time_offset_, time_buf_len_) == 0) {
        return;
    }

    if (stream_.is_open()) {
        stream_.close();
    }
    
    // get new time and file name
    memcpy(time_, log + time_offset_, time_buf_len_);
    std::string file_name(file_name_);
    file_name.append(".");
    file_name.append(time_, time_buf_len_);
    file_name.append(".log");

    history_file_names_.push(file_name);
    CheckExpireFiles();

    // open new log file
    stream_.open(file_name.c_str(), std::ios::app | std::ios::out);
}

void FileLogger::CheckExpireFiles() {
    // delete expire files
    while (history_file_names_.size() > max_file_num_) {
        std::string del_file = history_file_names_.front();
        history_file_names_.pop();
        std::remove(del_file.c_str());
    }
}

}
}