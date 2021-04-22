#include <gtest/gtest.h>
#include "common/log/log.h"
#include "common/log/file_logger.h"
#include "common/log/stdout_logger.h"

TEST(stdlogger_stream_utest, print1) {
    std::shared_ptr<quicx::Logger> log = std::make_shared<quicx::StdoutLogger>();
    quicx::LOG_SET(log);
    quicx::LOG_SET_LEVEL(quicx::LL_ERROR);
    
    quicx::LOG_DEBUG_S << "it is a debug test log.";
    quicx::LOG_INFO_S << "it is a info test num";
    quicx::LOG_WARN_S <<  "it is a warn test num";
    quicx::LOG_ERROR_S <<  "it is a error test num";
    quicx::LOG_FATAL_S <<  "it is a fatal test num";
}


TEST(filelogger_stream_utest, debug) {
    std::shared_ptr<quicx::Logger> file_log = std::make_shared<quicx::FileLogger>();
    std::shared_ptr<quicx::Logger> std_log = std::make_shared<quicx::StdoutLogger>();
    file_log->SetLogger(std_log);
    quicx::LOG_SET(file_log);
    quicx::LOG_SET_LEVEL(quicx::LL_DEBUG);
    
    quicx::LOG_DEBUG_S << "it is a debug test log.";
    quicx::LOG_INFO_S << "it is a info test num";
    quicx::LOG_WARN_S <<  "it is a warn test num";
    quicx::LOG_ERROR_S <<  "it is a error test num";
    quicx::LOG_FATAL_S <<  "it is a fatal test num";
}