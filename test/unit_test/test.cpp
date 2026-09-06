// winsock2.h must precede any header that pulls in <windows.h> (e.g. gtest),
// otherwise winsock 1/2 definitions clash.
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <gtest/gtest.h>

#ifdef _WIN32
class WinsockEnvironment: public ::testing::Environment {
public:
    void SetUp() override {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }

    void TearDown() override { WSACleanup(); }
};
#endif

#include "common/log/file_logger.h"
#include "common/log/log.h"
#include "common/log/stdout_logger.h"

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);

#ifdef _WIN32
    testing::AddGlobalTestEnvironment(new WinsockEnvironment());
#endif
    std::shared_ptr<quicx::common::Logger> file_log = std::make_shared<quicx::common::FileLogger>("test.log");
    std::shared_ptr<quicx::common::Logger> std_log = std::make_shared<quicx::common::StdoutLogger>();
    file_log->SetLogger(std_log);
    LOG_SET(file_log);
    LOG_SET_LEVEL(quicx::common::LogLevel::kDebug);

    int ret = RUN_ALL_TESTS();

    return ret;
}