#ifndef FUZZ_QUIC_PACKET_TEST_CRYPTOGRAPHER
#define FUZZ_QUIC_PACKET_TEST_CRYPTOGRAPHER

#include "quic/connection/type.h"
#include "common/util/singleton.h"
#include "quic/crypto/if_cryptographer.h"
#include "quic/connection/connection_id_generator.h"

static const uint8_t kBufLength = 128;
static uint8_t kData[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30};
static uint64_t kOffset = 1024;
static uint8_t kLevel = 1;

class PacketTest:
    public quicx::common::Singleton<PacketTest> {
public:
    PacketTest() {
        uint8_t dcid[quicx::quic::kMaxCidLength] = {0};
        quicx::quic::ConnectionIDGenerator::Instance().Generator(dcid, quicx::quic::kMaxCidLength);

        cli_cryptographer_ = quicx::quic::MakeCryptographer(quicx::quic::kCipherIdAes128GcmSha256);
        if (cli_cryptographer_->InstallInitSecret(dcid, quicx::quic::kMaxCidLength, quicx::quic::kInitialSalt.data(), quicx::quic::kInitialSalt.size(), false)
            != quicx::quic::ICryptographer::Result::kOk) {
            abort();
        }

        ser_cryptographer_ = quicx::quic::MakeCryptographer(quicx::quic::kCipherIdAes128GcmSha256);
        if (ser_cryptographer_->InstallInitSecret(dcid, quicx::quic::kMaxCidLength, quicx::quic::kInitialSalt.data(), quicx::quic::kInitialSalt.size(), true) 
            != quicx::quic::ICryptographer::Result::kOk) {
            abort();
        }
    }
    ~PacketTest() {}

    std::shared_ptr<quicx::quic::ICryptographer> GetTestClientCryptographer() { return cli_cryptographer_; }
    std::shared_ptr<quicx::quic::ICryptographer> GetTestServerCryptographer() { return ser_cryptographer_; }

private:
    std::shared_ptr<quicx::quic::ICryptographer> cli_cryptographer_;
    std::shared_ptr<quicx::quic::ICryptographer> ser_cryptographer_;
};

#endif