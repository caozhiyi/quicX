#include <gtest/gtest.h>
#include <memory>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"
#include "quic/crypto/aes_128_gcm_cryptographer.h"
#include "quic/crypto/chacha20_poly1305_cryptographer.h"

namespace quicx {
namespace quic {
namespace {

static bool EncryptOnce(std::shared_ptr<ICryptographer> enc,
                        std::shared_ptr<common::SingleBlockBuffer>& out_cipher) {
  auto chunk = std::make_shared<common::StandaloneBufferChunk>(2048);
  auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);
  // Build small plaintext
  auto span = buffer->GetWritableSpan();
  for (size_t i = 0; i < 64; ++i) span.GetStart()[i] = static_cast<uint8_t>(i + 1);
  buffer->MoveWritePt(64);
  // AAD
  uint8_t aad_raw[8] = {0,1,2,3,4,5,6,7};
  common::BufferSpan aad(aad_raw, aad_raw + sizeof(aad_raw));
  // Encrypt
  out_cipher = std::make_shared<common::SingleBlockBuffer>(chunk);
  auto pts = buffer->GetReadableSpan();
  return enc->EncryptPacket(5, aad, pts, out_cipher) == ICryptographer::Result::kOk;
}

static ICryptographer::Result DecryptOnce(std::shared_ptr<ICryptographer> dec,
                                          std::shared_ptr<common::SingleBlockBuffer> cipher) {
  uint8_t aad_raw[8] = {0,1,2,3,4,5,6,7};
  common::BufferSpan aad(aad_raw, aad_raw + sizeof(aad_raw));
  auto cts = cipher->GetReadableSpan();
  auto out = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(2048));
  return dec->DecryptPacket(5, aad, cts, out);
}

TEST(KeyUpdateTest, AES128_UpdateWriteServer_ReadClient) {
  // Secrets: baseA for server->client (srv write, cli read), baseB for client->server
  uint8_t baseA[32]; for (int i = 0; i < 32; ++i) baseA[i] = static_cast<uint8_t>(0x10 + i);
  uint8_t baseB[32]; for (int i = 0; i < 32; ++i) baseB[i] = static_cast<uint8_t>(0x80 + i);

  auto srv = std::make_shared<Aes128GcmCryptographer>();
  auto cli = std::make_shared<Aes128GcmCryptographer>();

  ASSERT_EQ(srv->InstallSecret(baseA, sizeof(baseA), true), ICryptographer::Result::kOk);   // srv write
  ASSERT_EQ(cli->InstallSecret(baseA, sizeof(baseA), false), ICryptographer::Result::kOk);  // cli read
  ASSERT_EQ(srv->InstallSecret(baseB, sizeof(baseB), false), ICryptographer::Result::kOk);  // srv read
  ASSERT_EQ(cli->InstallSecret(baseB, sizeof(baseB), true), ICryptographer::Result::kOk);   // cli write

  // Initial success
  std::shared_ptr<common::SingleBlockBuffer> ct_old;
  ASSERT_TRUE(EncryptOnce(srv, ct_old));
  ASSERT_EQ(DecryptOnce(cli, ct_old), ICryptographer::Result::kOk);

  // Prepare an "old" client to demonstrate failure after update
  auto cli_old = std::make_shared<Aes128GcmCryptographer>();
  ASSERT_EQ(cli_old->InstallSecret(baseA, sizeof(baseA), false), ICryptographer::Result::kOk);
  ASSERT_EQ(cli_old->InstallSecret(baseB, sizeof(baseB), true), ICryptographer::Result::kOk);

  // KeyUpdate on write(server) and read(client)
  ASSERT_EQ(srv->KeyUpdate(nullptr, 0, true), ICryptographer::Result::kOk);
  ASSERT_EQ(cli->KeyUpdate(nullptr, 0, false), ICryptographer::Result::kOk);

  // New packet should decrypt with new client read keys
  std::shared_ptr<common::SingleBlockBuffer> ct_new;
  ASSERT_TRUE(EncryptOnce(srv, ct_new));
  ASSERT_EQ(DecryptOnce(cli, ct_new), ICryptographer::Result::kOk);
  // Old client (old read) should fail on new packet
  ASSERT_NE(DecryptOnce(cli_old, ct_new), ICryptographer::Result::kOk);
  // New client (new read) should fail on old packet (since secrets rotated)
  ASSERT_NE(DecryptOnce(cli, ct_old), ICryptographer::Result::kOk);
}

TEST(KeyUpdateTest, ChaCha20_UpdateWriteClient_ReadServer) {
  uint8_t baseA[32]; for (int i = 0; i < 32; ++i) baseA[i] = static_cast<uint8_t>(0x21 + i);
  uint8_t baseB[32]; for (int i = 0; i < 32; ++i) baseB[i] = static_cast<uint8_t>(0xB0 + i);

  auto srv = std::make_shared<ChaCha20Poly1305Cryptographer>();
  auto cli = std::make_shared<ChaCha20Poly1305Cryptographer>();

  ASSERT_EQ(srv->InstallSecret(baseA, sizeof(baseA), true), ICryptographer::Result::kOk);   // srv write
  ASSERT_EQ(cli->InstallSecret(baseA, sizeof(baseA), false), ICryptographer::Result::kOk);  // cli read
  ASSERT_EQ(srv->InstallSecret(baseB, sizeof(baseB), false), ICryptographer::Result::kOk);  // srv read
  ASSERT_EQ(cli->InstallSecret(baseB, sizeof(baseB), true), ICryptographer::Result::kOk);   // cli write

  // Initial success
  std::shared_ptr<common::SingleBlockBuffer> ct_old;
  ASSERT_TRUE(EncryptOnce(cli, ct_old));
  ASSERT_EQ(DecryptOnce(srv, ct_old), ICryptographer::Result::kOk);

  // Prepare an old server
  auto srv_old = std::make_shared<ChaCha20Poly1305Cryptographer>();
  ASSERT_EQ(srv_old->InstallSecret(baseA, sizeof(baseA), true), ICryptographer::Result::kOk);
  ASSERT_EQ(srv_old->InstallSecret(baseB, sizeof(baseB), false), ICryptographer::Result::kOk);

  // KeyUpdate on write(client) and read(server)
  ASSERT_EQ(cli->KeyUpdate(nullptr, 0, true), ICryptographer::Result::kOk);
  ASSERT_EQ(srv->KeyUpdate(nullptr, 0, false), ICryptographer::Result::kOk);

  std::shared_ptr<common::SingleBlockBuffer> ct_new;
  ASSERT_TRUE(EncryptOnce(cli, ct_new));
  ASSERT_EQ(DecryptOnce(srv, ct_new), ICryptographer::Result::kOk);
  ASSERT_NE(DecryptOnce(srv_old, ct_new), ICryptographer::Result::kOk);
  ASSERT_NE(DecryptOnce(srv, ct_old), ICryptographer::Result::kOk);
}

} // namespace
} // namespace quic
} // namespace quicx


