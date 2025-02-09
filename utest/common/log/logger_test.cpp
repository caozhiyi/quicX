#include <gtest/gtest.h>
#include "common/log/log.h"
#include "common/log/file_logger.h"
#include "common/log/stdout_logger.h"

namespace quicx {
namespace common {
namespace {

TEST(stdlogger_utest, print1) {
    std::shared_ptr<Logger> log = std::make_shared<StdoutLogger>();
    LOG_SET(log);
    LOG_SET_LEVEL(LogLevel::kError);
    
    LOG_DEBUG("it is a debug test log.");
    LOG_INFO("%s %d", "it is a info test num", 100191);
    LOG_WARN("%s %d", "it is a warn test num", 100191);
    LOG_ERROR("%s %d", "it is a error test num", 100191);
    LOG_FATAL("%s %d", "it is a fatal test num", 100191);
}


TEST(filelogger_utest, debug) {
    std::shared_ptr<Logger> file_log = std::make_shared<FileLogger>("test.log");
    std::shared_ptr<Logger> std_log = std::make_shared<StdoutLogger>();
    file_log->SetLogger(std_log);
    LOG_SET(file_log);
    LOG_SET_LEVEL(LogLevel::kDebug);
    
    LOG_DEBUG("it is a debug test log.");
    LOG_INFO("%s %d", "it is a info test num", 100191);
    LOG_WARN("%s %d", "it is a warn test num", 100191);
    LOG_ERROR("%s %d", "it is a error test num", 100191);
    LOG_FATAL("%s %d", "it is a fatal test num", 100191);
}

}
}
}