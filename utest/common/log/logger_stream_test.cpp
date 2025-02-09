#include <gtest/gtest.h>
#include "common/log/log.h"
#include "common/log/file_logger.h"
#include "common/log/stdout_logger.h"

namespace quicx {
namespace common {
namespace {

TEST(stdlogger_stream_utest, print1) {
    std::shared_ptr<common::Logger> log = std::make_shared<common::StdoutLogger>();
    LOG_SET(log);
    LOG_SET_LEVEL(LogLevel::kError);
    
    LOG_DEBUG_S << "it is a debug test log.";
    LOG_INFO_S << "it is a info test num";
    LOG_WARN_S <<  "it is a warn test num";
    LOG_ERROR_S <<  "it is a error test num";
    LOG_FATAL_S <<  "it is a fatal test num";
}

TEST(filelogger_stream_utest, debug) {
    std::shared_ptr<Logger> file_log = std::make_shared<FileLogger>("test.log");
    std::shared_ptr<Logger> std_log = std::make_shared<StdoutLogger>();
    file_log->SetLogger(std_log);
    LOG_SET(file_log);
    LOG_SET_LEVEL(LogLevel::kDebug);
    
    LOG_DEBUG_S << "it is a debug test log.";
    LOG_INFO_S << "it is a info test num";
    LOG_WARN_S <<  "it is a warn test num";
    LOG_ERROR_S <<  "it is a error test num";
    LOG_FATAL_S <<  "it is a fatal test num";
}

TEST(filelogger_stream_utest, value) {
    std::shared_ptr<Logger> file_log = std::make_shared<FileLogger>("test.log");
    std::shared_ptr<Logger> std_log = std::make_shared<StdoutLogger>();
    file_log->SetLogger(std_log);
    LOG_SET(file_log);
    LOG_SET_LEVEL(LogLevel::kDebug);
    
    LOG_FATAL_S <<  "bool value:" << true;
    LOG_FATAL_S <<  "int8 value:" << int8_t(32);
    LOG_FATAL_S <<  "uint8 value:" << uint8_t(-1);
    LOG_FATAL_S <<  "int16 value:" << int16_t(128);
    LOG_FATAL_S <<  "uint16 value:" << uint16_t(-1);
    LOG_FATAL_S <<  "int32 value:" << int32_t(6451);
    LOG_FATAL_S <<  "uint32 value:" << uint32_t(-1);
    LOG_FATAL_S <<  "int64 value:" << int64_t(-515811548);
    LOG_FATAL_S <<  "uint64 value:" << uint64_t(-1);
    LOG_FATAL_S <<  "float value:" << float(1.1111111111111);
    LOG_FATAL_S <<  "double value:" << double(1.45516167894165);;
    LOG_FATAL_S <<  "string value:" << std::string("it is a test log");
    LOG_FATAL_S <<  "const char* value:" << "it is a test log";
    LOG_FATAL_S <<  "char value:" << 'N';
}

}
}
}