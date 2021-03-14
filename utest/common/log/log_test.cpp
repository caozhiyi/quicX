#include <gtest/gtest.h>
#include "common/log/stdout_log.h"

TEST(stdlog_utest, debug) {
    std::shared_ptr<quicx::Log> log = std::make_shared<quicx::StdoutLog>();
    quicx::LOG_SET(log);
    quicx::LOG_SET_LEVEL(quicx::LL_NULL);
    
    quicx::LOG_DEBUG("it is a debug test log.");
    quicx::LOG_INFO("%s %d", "it is a info test num", 100191);
    quicx::LOG_WARN("%s %d", "it is a warn test num", 100191);
    quicx::LOG_ERROR("%s %d", "it is a error test num", 100191);
    quicx::LOG_FATAL("%s %d", "it is a fatal test num", 100191);
}