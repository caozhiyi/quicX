#include <cstdlib>
#include "quic/connection/type.h"
#include "quic/frame/crypto_frame.h"
#include "utest/quic/packet/common_test_frame.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {
namespace quic {

static uint8_t kData[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30};
static uint64_t kOffset = 1024;
static uint8_t kLevel = 1;

PacketTest::PacketTest() {
    uint8_t dcid[kMaxCidLength] = {0};
    ConnectionIDGenerator::Instance().Generator(dcid, kMaxCidLength);

    cli_cryptographer_ = MakeCryptographer(kCipherIdAes128GcmSha256);
    if (!cli_cryptographer_->InstallInitSecret(dcid, kMaxCidLength, kInitialSalt, sizeof(kInitialSalt), false)) {
        abort();
    }

    ser_cryptographer_ = MakeCryptographer(kCipherIdAes128GcmSha256);
    if (!ser_cryptographer_->InstallInitSecret(dcid, kMaxCidLength, kInitialSalt, sizeof(kInitialSalt), true)) {
        abort();
    }
}

std::shared_ptr<IFrame> PacketTest::GetTestFrame() {
    std::shared_ptr<CryptoFrame> frame = std::make_shared<CryptoFrame>();
    frame->SetData(kData, sizeof(kData));
    frame->SetOffset(kOffset);
    frame->SetEncryptionLevel(kLevel);

    return frame;
}

bool PacketTest::CheckTestFrame(std::shared_ptr<IFrame> f) {
    std::shared_ptr<CryptoFrame> frame = std::dynamic_pointer_cast<CryptoFrame>(f);
    if (frame->GetOffset() != kOffset) {
        return false;
    }
    
    if (frame->GetLength() != sizeof(kData)) {
        return false;
    }
    
    uint8_t* data = frame->GetData();
    for (size_t i = 0; i < frame->GetLength(); i++) {
        if (*(data + i) != kData[i]) {
            return false;
        }
    }

    return true;
}

std::shared_ptr<ICryptographer> PacketTest::GetTestClientCryptographer() {
    return cli_cryptographer_;
    
}

std::shared_ptr<ICryptographer> PacketTest::GetTestServerCryptographer() {
    return ser_cryptographer_;
}

}
}