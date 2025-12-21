#include <gtest/gtest.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>

class WinsockEnvironment: public ::testing::Environment {
public:
    void SetUp() override {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }

    void TearDown() override { WSACleanup(); }
};
#endif

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);

#ifdef _WIN32
    testing::AddGlobalTestEnvironment(new WinsockEnvironment());
#endif
    int ret = RUN_ALL_TESTS();

    return ret;
}