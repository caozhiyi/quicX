#include "quic/connection/type.h"
#include "quic/frame/crypto_frame.h"
#include "utest/quic/packet/common_test_frame.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {

static uint8_t __data[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30};
static uint64_t __offset = 1024;
static uint8_t __level = 1;

PacketTest::PacketTest() {
    uint8_t dcid[__max_cid_length] = {0};
    ConnectionIDGenerator::Instance().Generator(dcid, __max_cid_length);

    _cli_cryptographer = MakeCryptographer(CI_TLS1_CK_AES_128_GCM_SHA256);
    if (!_cli_cryptographer->InstallInitSecret(dcid, __max_cid_length, __initial_slat, sizeof(__initial_slat), false)) {
        abort();
    }

    _ser_cryptographer = MakeCryptographer(CI_TLS1_CK_AES_128_GCM_SHA256);
    if (!_ser_cryptographer->InstallInitSecret(dcid, __max_cid_length, __initial_slat, sizeof(__initial_slat), true)) {
        abort();
    }
}

std::shared_ptr<IFrame> PacketTest::GetTestFrame() {
    std::shared_ptr<CryptoFrame> frame = std::make_shared<CryptoFrame>();
    frame->SetData(__data, sizeof(__data));
    frame->SetOffset(__offset);
    frame->SetEncryptionLevel(__level);

    return frame;
}

bool PacketTest::CheckTestFrame(std::shared_ptr<IFrame> f) {
    std::shared_ptr<CryptoFrame> frame = std::dynamic_pointer_cast<CryptoFrame>(f);
    if (frame->GetEncryptionLevel() != __level) {
        return false;
    }
    
    if (frame->GetOffset() != __offset) {
        return false;
    }
    
    if (frame->GetLength() != sizeof(__data)) {
        return false;
    }
    
    uint8_t* data = frame->GetData();
    for (size_t i = 0; i < frame->GetLength(); i++) {
        if (*(data + i) != __data[i]) {
            return false;
        }
    }

    return true;
}

std::shared_ptr<ICryptographer> PacketTest::GetTestClientCryptographer() {
    return _cli_cryptographer;
    
}

std::shared_ptr<ICryptographer> PacketTest::GetTestServerCryptographer() {
    return _ser_cryptographer;
}

}
