#include "file_logger.h"
#include "common/util/time.h"

namespace quicx {

FileLogger::FileLogger(const std::string& file) {
    _file_name = file + "." + GetFormatTime() + ".log";
    _stream.open(_file_name.c_str(), std::ios::app | std::ios::out);
    Start();
}

FileLogger::~FileLogger() {
    Stop();
    Join();
    _stream.close();
}

void FileLogger::Run() {
     while (!_stop) {
        auto log = Pop();
        if (log) {
            if (_stream.is_open()) {
                _stream.write(log->_log, log->_len);
                _stream.put('\n');
                _stream.flush();
            }

        } else {
            break;
        }
    }
}

void FileLogger::Stop() {
    _stop = true;
    Push(nullptr);
}

void FileLogger::Debug(std::shared_ptr<Log>& log) {
    Push(log);

    if (_logger) {
        _logger->Debug(log);
    }
}

void FileLogger::Info(std::shared_ptr<Log>& log) {
    Push(log);

    if (_logger) {
        _logger->Info(log);
    }
}

void FileLogger::Warn(std::shared_ptr<Log>& log) {
    Push(log);

    if (_logger) {
        _logger->Warn(log);
    }
}

void FileLogger::Error(std::shared_ptr<Log>& log) {
    Push(log);

    if (_logger) {
        _logger->Error(log);
    }
}

void FileLogger::Fatal(std::shared_ptr<Log>& log) {
    Push(log);

    if (_logger) {
        _logger->Fatal(log);
    }
}

}