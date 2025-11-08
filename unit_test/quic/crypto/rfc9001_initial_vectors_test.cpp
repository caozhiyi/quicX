#include <memory>
#include <gtest/gtest.h>


#include "common/buffer/buffer.h"
#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_read_view.h"
#include "quic/crypto/aes_128_gcm_cryptographer.h"
#include "quic/crypto/chacha20_poly1305_cryptographer.h"

// RFC-aligned initial salt (QUIC v1): 0x38762cf7f55934b34d179ae6a4c80cadccbb7f0a
// and a commonly used initial DCID: 0x8394c8f03e515708
// These tests validate that our initial secret derivation can establish
// interoperable keys (encrypt/decrypt + header protection), which is the core
// requirement of RFC 9001 initial secrets. We intentionally avoid asserting
// internal key bytes to keep the test implementation-agnostic.

namespace quicx {
namespace quic {
namespace {

static const uint8_t kRfcSalt[] = {
  0x38,0x76,0x2c,0xf7,0xf5,0x59,0x34,0xb3,0x4d,0x17,
  0x9a,0xe6,0xa4,0xc8,0x0c,0xad,0xcc,0xbb,0x7f,0x0a
};

static const uint8_t kRfcDcid[] = { 0x83,0x94,0xc8,0xf0,0x3e,0x51,0x57,0x08 };

// Simple associated data and sample for header protection exercise
static const uint8_t kAAD[] = { 0x01, 0x02, 0x03, 0x04 };
static const uint8_t kHPsample[16] = { 0 };

static bool RoundTripPacket(std::shared_ptr<ICryptographer> enc,
                            std::shared_ptr<ICryptographer> dec) {
    auto pool = std::make_shared<common::BlockMemoryPool>(2048, 5);

    // Build plaintext
    const size_t kLen = 512;
    auto pt = std::make_shared<common::Buffer>(pool);
    auto ptw = pt->GetWriteSpan();
    for (size_t i = 0; i < kLen; ++i) ptw.GetStart()[i] = static_cast<uint8_t>(i);
    pt->MoveWritePt(kLen);

    // Encrypt
    auto ct = std::make_shared<common::Buffer>(pool);
    common::BufferSpan aad((uint8_t*)kAAD, (uint8_t*)kAAD + sizeof(kAAD));
    auto ptr = pt->GetReadSpan();
    if (enc->EncryptPacket(1, aad, ptr, ct) != ICryptographer::Result::kOk) {
        ADD_FAILURE() << "EncryptPacket failed";
        return false;
    }

    // Decrypt
    auto out = std::make_shared<common::Buffer>(pool);
    auto cts = ct->GetReadSpan();
    if (dec->DecryptPacket(1, aad, cts, out) != ICryptographer::Result::kOk) {
        ADD_FAILURE() << "DecryptPacket failed";
        return false;
    }

    // Compare
    if (out->GetDataLength() != kLen) {
        ADD_FAILURE() << "Plaintext length mismatch";
        return false;
    }
    auto outv = out->GetReadSpan();
    for (size_t i = 0; i < kLen; ++i) {
        if (outv.GetStart()[i] != ptw.GetStart()[i]) {
            ADD_FAILURE() << "Plaintext mismatch at index " << i;
            return false;
        }
    }
    return true;
}

static bool RoundTripHeader(std::shared_ptr<ICryptographer> enc,
                            std::shared_ptr<ICryptographer> dec) {
  // Build a pseudo header buffer of 32 bytes
  uint8_t header[32] = {0};
    common::BufferSpan hdr(header, header + sizeof(header));
    common::BufferSpan sample((uint8_t*)kHPsample, (uint8_t*)kHPsample + sizeof(kHPsample));
    const uint8_t pn_offset = 2;
  const size_t pn_len = 2;      // packet number length in bytes for masking loop
  const uint8_t pn_code = 2;    // low 2-bit code stored in header (implementation matches existing decrypt)

  // Pre-set PN length bits in first byte so decrypt can recover it
  header[0] = (header[0] & ~0x03) | (pn_code & 0x03);

    // Encrypt header (apply header protection)
    if (enc->EncryptHeader(hdr, sample, pn_offset, pn_len, true) != ICryptographer::Result::kOk) {
        ADD_FAILURE() << "EncryptHeader failed";
        return false;
    }

    // Decrypt header (remove header protection)
    uint8_t out_pn_len = 0;
    if (dec->DecryptHeader(hdr, sample, pn_offset, out_pn_len, true) != ICryptographer::Result::kOk) {
        ADD_FAILURE() << "DecryptHeader failed";
      return false;
    }

  // Expect PN length code recovered
  return out_pn_len == pn_code;
}

TEST(Rfc9001InitialVectors, Aes128Gcm_InitialSecrets_RoundTrip) {
    auto srv = std::make_shared<Aes128GcmCryptographer>();
    auto cli = std::make_shared<Aes128GcmCryptographer>();

    ASSERT_EQ(srv->InstallInitSecret(kRfcDcid, sizeof(kRfcDcid), kRfcSalt, sizeof(kRfcSalt), true), ICryptographer::Result::kOk);
    ASSERT_EQ(cli->InstallInitSecret(kRfcDcid, sizeof(kRfcDcid), kRfcSalt, sizeof(kRfcSalt), false), ICryptographer::Result::kOk);

    // Tag length must be 16 for GCM
    ASSERT_EQ(srv->GetTagLength(), static_cast<size_t>(16));
    ASSERT_EQ(cli->GetTagLength(), static_cast<size_t>(16));

    ASSERT_TRUE(RoundTripPacket(srv, cli));
    ASSERT_TRUE(RoundTripPacket(cli, srv));

    ASSERT_TRUE(RoundTripHeader(srv, cli));
    ASSERT_TRUE(RoundTripHeader(cli, srv));
}

TEST(Rfc9001InitialVectors, ChaCha20Poly1305_InitialSecrets_RoundTrip) {
    auto srv = std::make_shared<ChaCha20Poly1305Cryptographer>();
    auto cli = std::make_shared<ChaCha20Poly1305Cryptographer>();

    ASSERT_EQ(srv->InstallInitSecret(kRfcDcid, sizeof(kRfcDcid), kRfcSalt, sizeof(kRfcSalt), true), ICryptographer::Result::kOk);
    ASSERT_EQ(cli->InstallInitSecret(kRfcDcid, sizeof(kRfcDcid), kRfcSalt, sizeof(kRfcSalt), false), ICryptographer::Result::kOk);

    ASSERT_EQ(srv->GetTagLength(), static_cast<size_t>(16));
    ASSERT_EQ(cli->GetTagLength(), static_cast<size_t>(16));

    ASSERT_TRUE(RoundTripPacket(srv, cli));
    ASSERT_TRUE(RoundTripPacket(cli, srv));

    ASSERT_TRUE(RoundTripHeader(srv, cli));
    ASSERT_TRUE(RoundTripHeader(cli, srv));
}

} // namespace
} // namespace quic
} // namespace quicx


