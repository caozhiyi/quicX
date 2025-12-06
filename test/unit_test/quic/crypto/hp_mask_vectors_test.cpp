#include <gtest/gtest.h>
#include <openssl/evp.h>
#include <memory>

#include "quic/crypto/hkdf.h"
#include "quic/crypto/type.h"
#include "quic/crypto/aes_128_gcm_cryptographer.h"


namespace quicx {
namespace quic {
namespace {

static void BuildHeader(uint8_t* header, size_t header_len, uint8_t pn_code, uint8_t pn_offset, size_t pn_len) {
  memset(header, 0, header_len);
  header[0] = (header[0] & ~0x03) | (pn_code & 0x03);
  // Packet number bytes zero so encrypted bytes equal mask[1..4]
  (void)pn_len; (void)pn_offset;
}

static void ExpectHpMaskAes128(const uint8_t* hp_key,
                               const uint8_t* sample,
                               const uint8_t* header_before,
                               const uint8_t* header_after,
                               uint8_t pn_offset,
                               size_t pn_len) {
  // Compute expected mask via AES-ECB
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  ASSERT_NE(ctx, nullptr);
  ASSERT_EQ(EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr, hp_key, nullptr), 1);
  EVP_CIPHER_CTX_set_padding(ctx, 0);
  uint8_t block_out[16] = {0}; int outlen = 0, fin = 0;
  ASSERT_EQ(EVP_EncryptUpdate(ctx, block_out, &outlen, sample, 16), 1);
  ASSERT_EQ(EVP_EncryptFinal_ex(ctx, block_out + outlen, &fin), 1);
  EVP_CIPHER_CTX_free(ctx);

  // Compare mask low 5 bits and PN bytes
  uint8_t expected0 = block_out[0] & 0x1f;
  uint8_t actual0 = (header_before[0] ^ header_after[0]) & 0x1f;
  ASSERT_EQ(expected0, actual0);
  for (size_t i = 0; i < pn_len; ++i) {
    ASSERT_EQ(header_after[pn_offset + i], block_out[1 + i]);
  }
}

#ifdef EVP_chacha20
static void ExpectHpMaskChaCha20(const uint8_t* hp_key,
                                 const uint8_t* sample,
                                 const uint8_t* header_before,
                                 const uint8_t* header_after,
                                 uint8_t pn_offset,
                                 size_t pn_len) {
  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  ASSERT_NE(ctx, nullptr);
  uint8_t iv[16] = {0};
  memcpy(iv + 4, sample, 12);
  ASSERT_EQ(EVP_EncryptInit_ex(ctx, EVP_chacha20(), nullptr, hp_key, iv), 1);
  uint8_t stream5[5] = {0}; int outlen = 0;
  ASSERT_EQ(EVP_EncryptUpdate(ctx, stream5, &outlen, kHeaderMask.data(), 5), 1);
  EVP_CIPHER_CTX_free(ctx);

  uint8_t expected0 = stream5[0] & 0x1f;
  uint8_t actual0 = (header_before[0] ^ header_after[0]) & 0x1f;
  ASSERT_EQ(expected0, actual0);
  for (size_t i = 0; i < pn_len; ++i) {
    ASSERT_EQ(header_after[pn_offset + i], stream5[1 + i]);
  }
}
#endif

TEST(HeaderProtectionMask, AES128ECB_Vector) {
  // Arrange cryptographer with a known base secret.
  uint8_t base_secret[32]; for (int i = 0; i < 32; ++i) base_secret[i] = static_cast<uint8_t>(i);
  auto enc = std::make_shared<Aes128GcmCryptographer>();
  ASSERT_EQ(enc->InstallSecret(base_secret, sizeof(base_secret), true), ICryptographer::Result::kOk);

  // Derive expected hp key using same HKDF label/digest
  uint8_t hp_key[16] = {0};
  ASSERT_TRUE(Hkdf::HkdfExpand(hp_key, sizeof(hp_key), base_secret, sizeof(base_secret),
                               kTlsLabelHp.data(), kTlsLabelHp.size(), EVP_sha256()));

  // Build header and sample
  uint8_t header[20]; const uint8_t pn_offset = 2; const size_t pn_len = 4; const uint8_t pn_code = 0x03;
  BuildHeader(header, sizeof(header), pn_code, pn_offset, pn_len);
  uint8_t before[20]; memcpy(before, header, sizeof(header));
  uint8_t sample[16] = {0};

  // Apply header protection via cryptographer
  common::BufferSpan hdr(header, header + sizeof(header));
  common::BufferSpan samp(sample, sample + sizeof(sample));
  ASSERT_EQ(enc->EncryptHeader(hdr, samp, pn_offset, pn_len, true), ICryptographer::Result::kOk);

  // Assert mask matches expected
  ExpectHpMaskAes128(hp_key, sample, before, header, pn_offset, pn_len);
}

#ifdef EVP_chacha20
TEST(HeaderProtectionMask, ChaCha20_Vector) {
  // Arrange cryptographer with a known base secret.
  uint8_t base_secret[32]; for (int i = 0; i < 32; ++i) base_secret[i] = static_cast<uint8_t>(0xA0 + i);
  auto enc = std::make_shared<ChaCha20Poly1305Cryptographer>();
  ASSERT_EQ(enc->InstallSecret(base_secret, sizeof(base_secret), true), ICryptographer::Result::kOk);

  // Derive expected hp key using same HKDF label/digest (ChaCha uses SHA-256)
  uint8_t hp_key[32] = {0};
  ASSERT_TRUE(Hkdf::HkdfExpand(hp_key, sizeof(hp_key), base_secret, sizeof(base_secret),
                               kTlsLabelHp.data(), kTlsLabelHp.size(), EVP_sha256()));

  // Build header and sample
  uint8_t header[20]; const uint8_t pn_offset = 2; const size_t pn_len = 4; const uint8_t pn_code = 0x03;
  BuildHeader(header, sizeof(header), pn_code, pn_offset, pn_len);
  uint8_t before[20]; memcpy(before, header, sizeof(header));
  uint8_t sample[16] = {0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0,0,0,0};

  // Apply header protection via cryptographer
  common::BufferSpan hdr(header, header + sizeof(header));
  common::BufferSpan samp(sample, sample + sizeof(sample));
  ASSERT_EQ(enc->EncryptHeader(hdr, samp, pn_offset, pn_len, true), ICryptographer::Result::kOk);

  // Assert mask matches expected
  ExpectHpMaskChaCha20(hp_key, sample, before, header, pn_offset, pn_len);
}
#endif

} // namespace
} // namespace quic
} // namespace quicx


