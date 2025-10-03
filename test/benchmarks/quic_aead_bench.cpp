#if defined(QUICX_ENABLE_BENCHMARKS)
#include <benchmark/benchmark.h>
#include <vector>
#include <cstring>

#include "quic/crypto/aes_128_gcm_cryptographer.h"
#include "common/buffer/buffer.h"
#include "common/buffer/buffer_span.h"

namespace quicx {
namespace quic {

static void Fill(uint8_t* p, size_t n, uint8_t v) { memset(p, v, n); }

static void BM_AEAD_EncryptDecryptPacket(benchmark::State& state) {
    const size_t len = static_cast<size_t>(state.range(0));
    std::vector<uint8_t> secret(32), ad(16), plain(len), cipher(len + 32);
    Fill(secret.data(), secret.size(), 0x11);
    Fill(ad.data(), ad.size(), 0x22);
    Fill(plain.data(), plain.size(), 0xAA);

    Aes128GcmCryptographer aead;
    // Install same secret as both read and write for benchmark simplicity
    aead.InstallSecret(secret.data(), secret.size(), /*is_write*/true);
    aead.InstallSecret(secret.data(), secret.size(), /*is_write*/false);

    for (auto _ : state) {
        // Encrypt
        common::BufferSpan ad_span(ad.data(), (uint32_t)ad.size());
        common::BufferSpan pt_span(plain.data(), (uint32_t)plain.size());
        auto out_cipher_buf = std::make_shared<common::Buffer>(cipher.data(), (uint32_t)cipher.size());
        auto res = aead.EncryptPacket(/*pn*/1, ad_span, pt_span, out_cipher_buf);
        benchmark::DoNotOptimize(res);

        // Decrypt
        common::BufferSpan ct_span(out_cipher_buf->GetData(), out_cipher_buf->GetDataLength());
        auto out_plain_buf = std::make_shared<common::Buffer>(plain.data(), (uint32_t)plain.size());
        res = aead.DecryptPacket(/*pn*/1, ad_span, ct_span, out_plain_buf);
        benchmark::DoNotOptimize(res);
    }
}

} // namespace quic
} // namespace quicx

BENCHMARK(quicx::quic::BM_AEAD_EncryptDecryptPacket)->Arg(1024)->Arg(16*1024)->Arg(64*1024);
BENCHMARK_MAIN();
#else
int main() { return 0; }
#endif


