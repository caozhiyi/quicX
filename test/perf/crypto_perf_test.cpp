// =============================================================================
// crypto_perf_test.cpp - AEAD / Header Protection / HKDF micro-benchmarks
// =============================================================================
//
// Isolates the per-packet cryptographic cost of quicX, which is usually the
// single largest CPU consumer on the hot path.
//
// Covers:
//   - AES-128-GCM / AES-256-GCM / ChaCha20-Poly1305 packet encrypt + decrypt
//     at 64 / 256 / 1200 / 4096 byte payloads
//   - Header protection encrypt + decrypt (runs on every packet)
//   - HKDF-Expand (backs every key derivation, including key update)
//
// Build / usage:
//   cmake -B build -DENABLE_PERF_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
//   cmake --build build --target crypto_perf_test -j
//   ./build/bin/perf/crypto_perf_test
//   ./build/bin/perf/crypto_perf_test --benchmark_filter="Aes128Gcm_Encrypt"
//
// =============================================================================

#if defined(QUICX_ENABLE_BENCHMARKS)

#include <benchmark/benchmark.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <openssl/evp.h>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

#include "quic/crypto/aes_128_gcm_cryptographer.h"
#include "quic/crypto/aes_256_gcm_cryptographer.h"
#include "quic/crypto/chacha20_poly1305_cryptographer.h"
#include "quic/crypto/hkdf.h"
#include "quic/crypto/type.h"

namespace quicx {
namespace perf {

// ===========================================================================
// Test fixtures
// ===========================================================================

// Deterministic "dcid" used to derive Initial secrets.
static const uint8_t kTestDcid[] = "quicx-crypto-perf";
// A fixed 32-byte traffic secret, used to exercise InstallSecret path.
static const uint8_t kTestTrafficSecret[32] = {
    0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
    0x98, 0xa9, 0xba, 0xcb, 0xdc, 0xed, 0xfe, 0x0f,
    0x01, 0x12, 0x23, 0x34, 0x45, 0x56, 0x67, 0x78,
    0x89, 0x9a, 0xab, 0xbc, 0xcd, 0xde, 0xef, 0xf0,
};

static std::vector<uint8_t> RandomBytes(size_t n) {
    std::vector<uint8_t> buf(n);
    std::mt19937 gen(0xC0FFEEu);
    std::uniform_int_distribution<uint32_t> dist(0, 255);
    for (auto& b : buf) b = static_cast<uint8_t>(dist(gen));
    return buf;
}

// Install a pair of (encrypter, decrypter) that share the same traffic secret.
// We route encrypt through the encrypter's write keys and decrypt through the
// decrypter's read keys to exercise both AEAD paths independently.
template <typename CryptoT>
static std::pair<std::shared_ptr<CryptoT>, std::shared_ptr<CryptoT>>
MakeCryptographerPair() {
    auto enc = std::make_shared<CryptoT>();
    auto dec = std::make_shared<CryptoT>();
    enc->InstallSecret(kTestTrafficSecret, sizeof(kTestTrafficSecret), /*is_write=*/true);
    enc->InstallSecret(kTestTrafficSecret, sizeof(kTestTrafficSecret), /*is_write=*/false);
    dec->InstallSecret(kTestTrafficSecret, sizeof(kTestTrafficSecret), /*is_write=*/true);
    dec->InstallSecret(kTestTrafficSecret, sizeof(kTestTrafficSecret), /*is_write=*/false);
    return {enc, dec};
}

// ===========================================================================
// Scenario A: AEAD Packet Encrypt (write path, "send side")
// ===========================================================================

template <typename CryptoT>
static void BM_Aead_EncryptPacket(benchmark::State& state) {
    const size_t payload_size = static_cast<size_t>(state.range(0));

    auto crypto = std::make_shared<CryptoT>();
    crypto->InstallSecret(kTestTrafficSecret, sizeof(kTestTrafficSecret), true);
    crypto->InstallSecret(kTestTrafficSecret, sizeof(kTestTrafficSecret), false);

    auto plaintext_bytes = RandomBytes(payload_size);
    auto ad_bytes = RandomBytes(32);  // Typical AD (header) size

    // Reusable output buffer (cleared each iteration to avoid Append semantics).
    const size_t out_cap = payload_size + 64;

    uint64_t pkt_num = 0;
    for (auto _ : state) {
        auto out = std::make_shared<common::SingleBlockBuffer>(
            std::make_shared<common::StandaloneBufferChunk>(out_cap));

        common::BufferSpan ad(ad_bytes.data(), ad_bytes.data() + ad_bytes.size());
        common::BufferSpan pt(plaintext_bytes.data(),
                              plaintext_bytes.data() + plaintext_bytes.size());

        auto r = crypto->EncryptPacket(pkt_num++, ad, pt, out);
        benchmark::DoNotOptimize(r);
        benchmark::DoNotOptimize(out->GetDataLength());
    }

    state.SetBytesProcessed(state.iterations() * payload_size);
    state.counters["payload_B"] = static_cast<double>(payload_size);
}

// ===========================================================================
// Scenario B: AEAD Packet Decrypt (read path, "recv side")
// ===========================================================================

template <typename CryptoT>
static void BM_Aead_DecryptPacket(benchmark::State& state) {
    const size_t payload_size = static_cast<size_t>(state.range(0));

    auto crypto = std::make_shared<CryptoT>();
    crypto->InstallSecret(kTestTrafficSecret, sizeof(kTestTrafficSecret), true);
    crypto->InstallSecret(kTestTrafficSecret, sizeof(kTestTrafficSecret), false);

    auto plaintext_bytes = RandomBytes(payload_size);
    auto ad_bytes = RandomBytes(32);

    // Pre-encrypt one packet (fixed pn=1). Then decrypt that same ciphertext
    // repeatedly in the hot loop. This isolates the decrypt CPU cost.
    const size_t out_cap = payload_size + 64;
    auto ct_store = std::make_shared<common::SingleBlockBuffer>(
        std::make_shared<common::StandaloneBufferChunk>(out_cap));
    {
        common::BufferSpan ad(ad_bytes.data(), ad_bytes.data() + ad_bytes.size());
        common::BufferSpan pt(plaintext_bytes.data(),
                              plaintext_bytes.data() + plaintext_bytes.size());
        auto r = crypto->EncryptPacket(/*pn=*/1, ad, pt, ct_store);
        if (r != quic::ICryptographer::Result::kOk) {
            state.SkipWithError("pre-encrypt failed");
            return;
        }
    }
    auto ct_span = ct_store->GetReadableSpan();
    std::vector<uint8_t> ct_bytes(ct_span.GetStart(),
                                  ct_span.GetStart() + ct_span.GetLength());

    for (auto _ : state) {
        auto out = std::make_shared<common::SingleBlockBuffer>(
            std::make_shared<common::StandaloneBufferChunk>(out_cap));

        // Reset a mutable span for each iteration (decrypt may move pointers).
        std::vector<uint8_t> ct_copy = ct_bytes;
        common::BufferSpan ad(ad_bytes.data(), ad_bytes.data() + ad_bytes.size());
        common::BufferSpan ct(ct_copy.data(), ct_copy.data() + ct_copy.size());

        auto r = crypto->DecryptPacket(/*pn=*/1, ad, ct, out);
        benchmark::DoNotOptimize(r);
        benchmark::DoNotOptimize(out->GetDataLength());
    }

    state.SetBytesProcessed(state.iterations() * payload_size);
    state.counters["payload_B"] = static_cast<double>(payload_size);
}

// ===========================================================================
// Scenario C: Header Protection (runs once per packet on each side)
// ===========================================================================

template <typename CryptoT>
static void BM_Aead_EncryptHeader(benchmark::State& state) {
    auto crypto = std::make_shared<CryptoT>();
    crypto->InstallInitSecret(
        kTestDcid, sizeof(kTestDcid),
        quic::kInitialSaltV1.data(), quic::kInitialSaltV1.size(),
        /*is_server=*/false);

    // Short header-ish bytes: [first_byte, 4-byte connection id, 2-byte pn]
    constexpr size_t kHdrLen = 16;
    std::vector<uint8_t> header_bytes(kHdrLen, 0);
    header_bytes[0] = 0x40;  // short header, no key phase
    // 16-byte sample typical for header protection.
    auto sample_bytes = RandomBytes(quic::kHeaderProtectSampleLength);

    const uint8_t pn_offset = 5;
    const size_t pn_len = 2;

    for (auto _ : state) {
        std::vector<uint8_t> hdr_copy = header_bytes;
        common::BufferSpan header(hdr_copy.data(), hdr_copy.data() + hdr_copy.size());
        common::BufferSpan sample(sample_bytes.data(),
                                  sample_bytes.data() + sample_bytes.size());

        auto r = crypto->EncryptHeader(header, sample, pn_offset, pn_len, /*is_short=*/true);
        benchmark::DoNotOptimize(r);
        benchmark::DoNotOptimize(hdr_copy.data());
    }
    state.SetItemsProcessed(state.iterations());
}

template <typename CryptoT>
static void BM_Aead_DecryptHeader(benchmark::State& state) {
    auto crypto = std::make_shared<CryptoT>();
    crypto->InstallInitSecret(
        kTestDcid, sizeof(kTestDcid),
        quic::kInitialSaltV1.data(), quic::kInitialSaltV1.size(),
        /*is_server=*/false);

    constexpr size_t kHdrLen = 16;
    std::vector<uint8_t> header_bytes(kHdrLen, 0);
    header_bytes[0] = 0x40;
    auto sample_bytes = RandomBytes(quic::kHeaderProtectSampleLength);

    const uint8_t pn_offset = 5;
    const size_t pn_len = 2;

    // Pre-encrypt the header once so the decrypt path has valid input.
    {
        common::BufferSpan header(header_bytes.data(), header_bytes.data() + header_bytes.size());
        common::BufferSpan sample(sample_bytes.data(),
                                  sample_bytes.data() + sample_bytes.size());
        crypto->EncryptHeader(header, sample, pn_offset, pn_len, true);
    }

    for (auto _ : state) {
        std::vector<uint8_t> hdr_copy = header_bytes;
        common::BufferSpan header(hdr_copy.data(), hdr_copy.data() + hdr_copy.size());
        common::BufferSpan sample(sample_bytes.data(),
                                  sample_bytes.data() + sample_bytes.size());

        uint8_t out_pn_len = 0;
        auto r = crypto->DecryptHeader(header, sample, pn_offset, out_pn_len, /*is_short=*/true);
        benchmark::DoNotOptimize(r);
        benchmark::DoNotOptimize(out_pn_len);
    }
    state.SetItemsProcessed(state.iterations());
}

// ===========================================================================
// Scenario D: HKDF-Expand (used by InstallSecret & KeyUpdate)
// ===========================================================================

static void BM_Hkdf_Expand_Sha256_32(benchmark::State& state) {
    // Emulate deriving a 32-byte key with SHA-256 and a short info label.
    const uint8_t info[] = {
        't', 'l', 's', '1', '3', ' ', 'q', 'u', 'i', 'c', ' ', 'k', 'e', 'y'};
    std::array<uint8_t, 32> out{};

    const EVP_MD* md = EVP_sha256();

    for (auto _ : state) {
        bool ok = quic::Hkdf::HkdfExpand(
            out.data(), out.size(),
            kTestTrafficSecret, sizeof(kTestTrafficSecret),
            info, sizeof(info),
            md);
        benchmark::DoNotOptimize(ok);
        benchmark::DoNotOptimize(out.data());
    }
    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * out.size());
}

static void BM_Hkdf_Expand_Sha384_48(benchmark::State& state) {
    const uint8_t info[] = {
        't', 'l', 's', '1', '3', ' ', 'q', 'u', 'i', 'c', ' ', 'k', 'e', 'y'};
    std::array<uint8_t, 48> out{};

    const EVP_MD* md = EVP_sha384();

    for (auto _ : state) {
        bool ok = quic::Hkdf::HkdfExpand(
            out.data(), out.size(),
            kTestTrafficSecret, sizeof(kTestTrafficSecret),
            info, sizeof(info),
            md);
        benchmark::DoNotOptimize(ok);
        benchmark::DoNotOptimize(out.data());
    }
    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * out.size());
}

// ===========================================================================
// Non-template wrappers
// ===========================================================================
//
// google-benchmark's BENCHMARK_TEMPLATE macro feeds __VA_ARGS__ (which
// contains commas for multi-token C++ names like `quicx::quic::Foo`) into
// BENCHMARK_PRIVATE_CONCAT(a,b,c), a 3-argument concat macro. The comma in
// a qualified type name therefore breaks the unique-id generation and
// causes duplicate-symbol errors. Work around it by wrapping each template
// instantiation in a plain (non-template) function and registering those
// via the simple BENCHMARK() macro.

#define QUICX_PERF_AEAD_WRAPPER(func, CryptoT, suffix)                     \
    static void func##_##suffix(benchmark::State& s) {                     \
        func<quic::CryptoT>(s);                                            \
    }

// AES-128-GCM
QUICX_PERF_AEAD_WRAPPER(BM_Aead_EncryptPacket, Aes128GcmCryptographer, Aes128Gcm)
QUICX_PERF_AEAD_WRAPPER(BM_Aead_DecryptPacket, Aes128GcmCryptographer, Aes128Gcm)
QUICX_PERF_AEAD_WRAPPER(BM_Aead_EncryptHeader, Aes128GcmCryptographer, Aes128Gcm)
QUICX_PERF_AEAD_WRAPPER(BM_Aead_DecryptHeader, Aes128GcmCryptographer, Aes128Gcm)
// AES-256-GCM
QUICX_PERF_AEAD_WRAPPER(BM_Aead_EncryptPacket, Aes256GcmCryptographer, Aes256Gcm)
QUICX_PERF_AEAD_WRAPPER(BM_Aead_DecryptPacket, Aes256GcmCryptographer, Aes256Gcm)
QUICX_PERF_AEAD_WRAPPER(BM_Aead_EncryptHeader, Aes256GcmCryptographer, Aes256Gcm)
QUICX_PERF_AEAD_WRAPPER(BM_Aead_DecryptHeader, Aes256GcmCryptographer, Aes256Gcm)
// ChaCha20-Poly1305
QUICX_PERF_AEAD_WRAPPER(BM_Aead_EncryptPacket, ChaCha20Poly1305Cryptographer, ChaCha20Poly1305)
QUICX_PERF_AEAD_WRAPPER(BM_Aead_DecryptPacket, ChaCha20Poly1305Cryptographer, ChaCha20Poly1305)
QUICX_PERF_AEAD_WRAPPER(BM_Aead_EncryptHeader, ChaCha20Poly1305Cryptographer, ChaCha20Poly1305)
QUICX_PERF_AEAD_WRAPPER(BM_Aead_DecryptHeader, ChaCha20Poly1305Cryptographer, ChaCha20Poly1305)

#undef QUICX_PERF_AEAD_WRAPPER

}  // namespace perf
}  // namespace quicx

// ===========================================================================
// Registration (one BENCHMARK per source line -- __LINE__ uniqueness)
// ===========================================================================

// --- AES-128-GCM ---------------------------------------------------------
BENCHMARK(quicx::perf::BM_Aead_EncryptPacket_Aes128Gcm)
    ->Arg(64)->Arg(256)->Arg(1200)->Arg(4096)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Aead_DecryptPacket_Aes128Gcm)
    ->Arg(64)->Arg(256)->Arg(1200)->Arg(4096)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Aead_EncryptHeader_Aes128Gcm)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Aead_DecryptHeader_Aes128Gcm)->Unit(benchmark::kNanosecond);

// --- AES-256-GCM ---------------------------------------------------------
BENCHMARK(quicx::perf::BM_Aead_EncryptPacket_Aes256Gcm)
    ->Arg(64)->Arg(256)->Arg(1200)->Arg(4096)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Aead_DecryptPacket_Aes256Gcm)
    ->Arg(64)->Arg(256)->Arg(1200)->Arg(4096)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Aead_EncryptHeader_Aes256Gcm)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Aead_DecryptHeader_Aes256Gcm)->Unit(benchmark::kNanosecond);

// --- ChaCha20-Poly1305 ---------------------------------------------------
BENCHMARK(quicx::perf::BM_Aead_EncryptPacket_ChaCha20Poly1305)
    ->Arg(64)->Arg(256)->Arg(1200)->Arg(4096)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Aead_DecryptPacket_ChaCha20Poly1305)
    ->Arg(64)->Arg(256)->Arg(1200)->Arg(4096)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Aead_EncryptHeader_ChaCha20Poly1305)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Aead_DecryptHeader_ChaCha20Poly1305)->Unit(benchmark::kNanosecond);

BENCHMARK(quicx::perf::BM_Hkdf_Expand_Sha256_32)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Hkdf_Expand_Sha384_48)->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();

#else
int main() { return 0; }
#endif
