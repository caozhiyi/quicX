#ifndef UTEST_QUIC_PACKET_HEADER_COMMON_TEST_FRAME
#define UTEST_QUIC_PACKET_HEADER_COMMON_TEST_FRAME

#include "common/util/singleton.h"
#include "quic/frame/if_frame.h"
#include "quic/crypto/if_cryptographer.h"

namespace quicx {
namespace quic {

static const uint8_t kBufLength = 128;
class PacketTest:
    public common::Singleton<PacketTest> {
public:
    PacketTest();
    ~PacketTest() {}
    static std::shared_ptr<IFrame> GetTestFrame();
    static bool CheckTestFrame(std::shared_ptr<IFrame> frame);

    std::shared_ptr<ICryptographer> GetTestClientCryptographer();
    std::shared_ptr<ICryptographer> GetTestServerCryptographer();

private:
    std::shared_ptr<ICryptographer> cli_cryptographer_;
    std::shared_ptr<ICryptographer> ser_cryptographer_;
};

}
}

#endif