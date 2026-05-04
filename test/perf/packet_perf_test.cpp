// =============================================================================
// packet_perf_test.cpp - Packet-level encode / decode micro-benchmarks
// =============================================================================
//
// The existing cpu_hotspot_test only exercises the frame layer. This benchmark
// suite targets the packet layer -- the end-to-end path from "I have a payload
// and a crypto context" to "bytes on the wire" and back.
//
// Covers:
//   - InitPacket encode / decode-without-crypto / decode-with-crypto
//   - HandshakePacket encode / decode-without-crypto
//   - Rtt1Packet   encode / decode-without-crypto
//   - PacketNumber variable-length encoding + truncated decoding
//   - Coalesced-packet decoding (two Initial packets in one datagram)
//
// Build / usage:
//   cmake -B build -DENABLE_PERF_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
//   cmake --build build --target packet_perf_test -j
//   ./build/bin/perf/packet_perf_test
//
// =============================================================================

#if defined(QUICX_ENABLE_BENCHMARKS)

#include <benchmark/benchmark.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

#include "quic/crypto/aes_128_gcm_cryptographer.h"
#include "quic/crypto/if_cryptographer.h"
#include "quic/crypto/type.h"

#include "quic/packet/handshake_packet.h"
#include "quic/packet/init_packet.h"
#include "quic/packet/packet_decode.h"
#include "quic/packet/packet_number.h"
#include "quic/packet/rtt_1_packet.h"
#include "quic/packet/header/header_flag.h"
#include "quic/packet/header/long_header.h"
#include "quic/packet/header/short_header.h"

#include "quic/frame/crypto_frame.h"
#include "quic/frame/if_frame.h"
#include "quic/frame/stream_frame.h"

namespace quicx {
namespace perf {

// ===========================================================================
// Helpers
// ===========================================================================

static constexpr size_t kScratchBuf = 4096;
static const uint8_t kDcid[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
static const uint8_t kScid[] = {0x11, 0x22, 0x33, 0x44};

static std::shared_ptr<common::SingleBlockBuffer> MakeSingleBlock(size_t cap = kScratchBuf) {
    return std::make_shared<common::SingleBlockBuffer>(
        std::make_shared<common::StandaloneBufferChunk>(cap));
}

static std::vector<uint8_t> RandomBytes(size_t n, uint32_t seed = 0xABCDu) {
    std::vector<uint8_t> buf(n);
    std::mt19937 gen(seed);
    std::uniform_int_distribution<uint32_t> dist(0, 255);
    for (auto& b : buf) b = static_cast<uint8_t>(dist(gen));
    return buf;
}

// Build one encrypted CRYPTO frame of the requested raw-bytes size.
static std::shared_ptr<quic::CryptoFrame> MakeCryptoFrame(size_t payload_bytes) {
    auto data_buf = MakeSingleBlock(payload_bytes + 32);
    auto data = RandomBytes(payload_bytes, 0xCAFEu);
    data_buf->Write(data.data(), static_cast<uint32_t>(data.size()));

    auto f = std::make_shared<quic::CryptoFrame>();
    f->SetOffset(0);
    f->SetEncryptionLevel(quic::PacketCryptoLevel::kInitialCryptoLevel);
    f->SetData(data_buf->GetSharedReadableSpan());
    return f;
}

// Construct a cryptographer pair installed with Initial secrets.
// Uses kDcid (stable) + QUIC v1 Initial salt so both sides derive the same keys.
struct CryptoPair {
    std::shared_ptr<quic::ICryptographer> client;  // used for encrypt (client->server)
    std::shared_ptr<quic::ICryptographer> server;  // used for decrypt
};

static CryptoPair MakeInitialCryptoPair() {
    CryptoPair p;
    p.client = std::make_shared<quic::Aes128GcmCryptographer>();
    p.server = std::make_shared<quic::Aes128GcmCryptographer>();
    p.client->InstallInitSecret(kDcid, sizeof(kDcid),
                                quic::kInitialSaltV1.data(), quic::kInitialSaltV1.size(),
                                /*is_server=*/false);
    p.server->InstallInitSecret(kDcid, sizeof(kDcid),
                                quic::kInitialSaltV1.data(), quic::kInitialSaltV1.size(),
                                /*is_server=*/true);
    return p;
}

// ===========================================================================
// Scenario 1: InitPacket Encode (no crypto)
// ===========================================================================

static void BM_Packet_InitPacket_EncodeNoCrypto(benchmark::State& state) {
    const size_t payload_bytes = static_cast<size_t>(state.range(0));
    auto frame = MakeCryptoFrame(payload_bytes);

    // Pre-encode the frame payload once; InitPacket will own a span pointing
    // at frame_buf's data.
    auto frame_buf = MakeSingleBlock(payload_bytes + 64);
    frame->Encode(frame_buf);
    common::SharedBufferSpan payload(frame_buf->GetSharedReadableSpan());

    for (auto _ : state) {
        quic::InitPacket packet;
        packet.GetHeader()->SetPacketNumberLength(2);
        packet.SetPacketNumber(42);
        packet.SetPayload(payload);

        auto out = MakeSingleBlock(payload_bytes + 256);
        bool ok = packet.Encode(out);
        benchmark::DoNotOptimize(ok);
        benchmark::DoNotOptimize(out->GetDataLength());
    }
    state.SetBytesProcessed(state.iterations() * payload_bytes);
}

// ===========================================================================
// Scenario 2: InitPacket Encode (with crypto)
// ===========================================================================

static void BM_Packet_InitPacket_EncodeWithCrypto(benchmark::State& state) {
    const size_t payload_bytes = static_cast<size_t>(state.range(0));
    auto pair = MakeInitialCryptoPair();

    auto frame = MakeCryptoFrame(payload_bytes);
    auto frame_buf = MakeSingleBlock(payload_bytes + 64);
    frame->Encode(frame_buf);
    common::SharedBufferSpan payload(frame_buf->GetSharedReadableSpan());

    for (auto _ : state) {
        quic::InitPacket packet;
        packet.GetHeader()->SetPacketNumberLength(2);
        packet.SetPacketNumber(state.iterations() & 0xFFFFu);
        packet.SetPayload(payload);
        packet.SetCryptographer(pair.client);

        auto out = MakeSingleBlock(payload_bytes + 256);
        bool ok = packet.Encode(out);
        benchmark::DoNotOptimize(ok);
        benchmark::DoNotOptimize(out->GetDataLength());
    }
    state.SetBytesProcessed(state.iterations() * payload_bytes);
}

// ===========================================================================
// Scenario 3: InitPacket Decode (without crypto)
// ===========================================================================
// Measures the pure parsing CPU cost: flag -> long header -> payload span.

static void BM_Packet_InitPacket_DecodeNoCrypto(benchmark::State& state) {
    const size_t payload_bytes = static_cast<size_t>(state.range(0));
    auto frame = MakeCryptoFrame(payload_bytes);

    auto frame_buf = MakeSingleBlock(payload_bytes + 64);
    frame->Encode(frame_buf);
    common::SharedBufferSpan payload(frame_buf->GetSharedReadableSpan());

    // Encode one packet to serve as the test input.
    quic::InitPacket tx_packet;
    tx_packet.GetHeader()->SetPacketNumberLength(2);
    tx_packet.SetPacketNumber(42);
    tx_packet.SetPayload(payload);

    auto encoded = MakeSingleBlock(payload_bytes + 256);
    tx_packet.Encode(encoded);

    std::vector<uint8_t> wire(encoded->GetDataLength());
    encoded->ReadNotMovePt(wire.data(), static_cast<uint32_t>(wire.size()));

    for (auto _ : state) {
        auto in = MakeSingleBlock(wire.size() + 16);
        in->Write(wire.data(), static_cast<uint32_t>(wire.size()));

        quic::HeaderFlag flag;
        bool f_ok = flag.DecodeFlag(in);

        quic::InitPacket rx_packet(flag.GetFlag());
        bool p_ok = rx_packet.DecodeWithoutCrypto(in);

        benchmark::DoNotOptimize(f_ok);
        benchmark::DoNotOptimize(p_ok);
        benchmark::DoNotOptimize(rx_packet.GetPacketNumber());
    }
    state.SetBytesProcessed(state.iterations() * wire.size());
}

// ===========================================================================
// Scenario 4: InitPacket Decode (with crypto) — full receive path
// ===========================================================================

static void BM_Packet_InitPacket_DecodeWithCrypto(benchmark::State& state) {
    const size_t payload_bytes = static_cast<size_t>(state.range(0));
    auto pair = MakeInitialCryptoPair();

    auto frame = MakeCryptoFrame(payload_bytes);
    auto frame_buf = MakeSingleBlock(payload_bytes + 64);
    frame->Encode(frame_buf);
    common::SharedBufferSpan payload(frame_buf->GetSharedReadableSpan());

    quic::InitPacket tx_packet;
    tx_packet.GetHeader()->SetPacketNumberLength(2);
    tx_packet.SetPacketNumber(42);
    tx_packet.SetPayload(payload);
    tx_packet.SetCryptographer(pair.client);

    auto encoded = MakeSingleBlock(payload_bytes + 256);
    if (!tx_packet.Encode(encoded)) {
        state.SkipWithError("tx encode failed");
        return;
    }

    std::vector<uint8_t> wire(encoded->GetDataLength());
    encoded->ReadNotMovePt(wire.data(), static_cast<uint32_t>(wire.size()));

    for (auto _ : state) {
        auto in = MakeSingleBlock(wire.size() + 16);
        in->Write(wire.data(), static_cast<uint32_t>(wire.size()));

        quic::HeaderFlag flag;
        flag.DecodeFlag(in);

        quic::InitPacket rx_packet(flag.GetFlag());
        rx_packet.SetCryptographer(pair.server);

        bool ok1 = rx_packet.DecodeWithoutCrypto(in);
        auto plaintext = MakeSingleBlock(payload_bytes + 128);
        bool ok2 = rx_packet.DecodeWithCrypto(plaintext);

        benchmark::DoNotOptimize(ok1);
        benchmark::DoNotOptimize(ok2);
        benchmark::DoNotOptimize(plaintext->GetDataLength());
    }
    state.SetBytesProcessed(state.iterations() * wire.size());
}

// ===========================================================================
// Scenario 5: HandshakePacket Encode (no crypto)
// ===========================================================================

static void BM_Packet_HandshakePacket_EncodeNoCrypto(benchmark::State& state) {
    const size_t payload_bytes = static_cast<size_t>(state.range(0));
    auto frame = MakeCryptoFrame(payload_bytes);

    auto frame_buf = MakeSingleBlock(payload_bytes + 64);
    frame->Encode(frame_buf);
    common::SharedBufferSpan payload(frame_buf->GetSharedReadableSpan());

    for (auto _ : state) {
        quic::HandshakePacket packet;
        packet.GetHeader()->SetPacketNumberLength(2);
        packet.SetPacketNumber(7);
        packet.SetPayload(payload);

        auto out = MakeSingleBlock(payload_bytes + 256);
        bool ok = packet.Encode(out);
        benchmark::DoNotOptimize(ok);
        benchmark::DoNotOptimize(out->GetDataLength());
    }
    state.SetBytesProcessed(state.iterations() * payload_bytes);
}

// ===========================================================================
// Scenario 6: Rtt1Packet Encode (no crypto, 1-RTT / short header)
// ===========================================================================

static void BM_Packet_Rtt1Packet_EncodeNoCrypto(benchmark::State& state) {
    const size_t payload_bytes = static_cast<size_t>(state.range(0));

    // Build an arbitrary STREAM frame payload of payload_bytes bytes.
    auto sf = std::make_shared<quic::StreamFrame>();
    sf->SetStreamID(4);
    sf->SetOffset(0);
    auto data_buf = MakeSingleBlock(payload_bytes + 32);
    auto data = RandomBytes(payload_bytes, 0xDEADu);
    data_buf->Write(data.data(), static_cast<uint32_t>(data.size()));
    sf->SetData(data_buf->GetSharedReadableSpan());

    auto frame_buf = MakeSingleBlock(payload_bytes + 64);
    sf->Encode(frame_buf);
    common::SharedBufferSpan payload(frame_buf->GetSharedReadableSpan());

    for (auto _ : state) {
        quic::Rtt1Packet packet;
        packet.GetHeader()->SetPacketNumberLength(2);
        packet.GetHeader()->SetDestinationConnectionId(kDcid, sizeof(kDcid));
        packet.SetPacketNumber(100);
        packet.SetPayload(payload);

        auto out = MakeSingleBlock(payload_bytes + 256);
        bool ok = packet.Encode(out);
        benchmark::DoNotOptimize(ok);
        benchmark::DoNotOptimize(out->GetDataLength());
    }
    state.SetBytesProcessed(state.iterations() * payload_bytes);
}

// ===========================================================================
// Scenario 7: PacketNumber variable-length encoding
// ===========================================================================
// Runs on every packet both on send (encode truncated pn) and recv (decode).

static void BM_Packet_PacketNumber_Encode(benchmark::State& state) {
    const uint64_t pn = static_cast<uint64_t>(state.range(0));
    const uint32_t pn_len = quic::PacketNumber::GetPacketNumberLength(pn);

    uint8_t buf[8] = {0};
    for (auto _ : state) {
        uint8_t* end = quic::PacketNumber::Encode(buf, pn_len, pn);
        benchmark::DoNotOptimize(end);
        benchmark::DoNotOptimize(buf);
    }
    state.SetItemsProcessed(state.iterations());
    state.counters["pn_len"] = static_cast<double>(pn_len);
}

static void BM_Packet_PacketNumber_DecodeTruncated(benchmark::State& state) {
    // Simulate recovering a full pn from a truncated one.
    const uint64_t largest = 1ull << 20;
    const uint64_t truncated = 0x12345;
    const uint64_t bits = 32;

    for (auto _ : state) {
        uint64_t full = quic::PacketNumber::Decode(largest, truncated, bits);
        benchmark::DoNotOptimize(full);
    }
    state.SetItemsProcessed(state.iterations());
}

// ===========================================================================
// Scenario 8: Coalesced packet decode
// ===========================================================================
// Two InitPackets packed back-to-back in one "datagram".

static void BM_Packet_Coalesced_Decode(benchmark::State& state) {
    const size_t payload_bytes = 256;
    auto frame = MakeCryptoFrame(payload_bytes);
    auto frame_buf = MakeSingleBlock(payload_bytes + 64);
    frame->Encode(frame_buf);
    common::SharedBufferSpan payload(frame_buf->GetSharedReadableSpan());

    auto encoded = MakeSingleBlock(8192);
    for (int i = 0; i < 2; ++i) {
        quic::InitPacket pkt;
        pkt.GetHeader()->SetPacketNumberLength(2);
        pkt.SetPacketNumber(100 + i);
        pkt.SetPayload(payload);
        if (!pkt.Encode(encoded)) {
            state.SkipWithError("coalesced encode failed");
            return;
        }
    }

    std::vector<uint8_t> wire(encoded->GetDataLength());
    encoded->ReadNotMovePt(wire.data(), static_cast<uint32_t>(wire.size()));

    for (auto _ : state) {
        auto in = MakeSingleBlock(wire.size() + 16);
        in->Write(wire.data(), static_cast<uint32_t>(wire.size()));

        std::vector<std::shared_ptr<quic::IPacket>> packets;
        bool ok = quic::DecodePackets(in, packets);

        benchmark::DoNotOptimize(ok);
        benchmark::DoNotOptimize(packets.size());
    }
    state.SetBytesProcessed(state.iterations() * wire.size());
}

// A/B probe: same InitPacket, but decoded via DecodePackets() -- lets us
// quantify the overhead of the dispatch layer vs. the manual fast path used
// in BM_Packet_InitPacket_DecodeNoCrypto.

static void BM_Packet_SinglePacket_DecodeViaDispatch(benchmark::State& state) {
    const size_t payload_bytes = 256;
    auto frame = MakeCryptoFrame(payload_bytes);
    auto frame_buf = MakeSingleBlock(payload_bytes + 64);
    frame->Encode(frame_buf);
    common::SharedBufferSpan payload(frame_buf->GetSharedReadableSpan());

    quic::InitPacket tx;
    tx.GetHeader()->SetPacketNumberLength(2);
    tx.SetPacketNumber(42);
    tx.SetPayload(payload);

    auto encoded = MakeSingleBlock(payload_bytes + 256);
    tx.Encode(encoded);

    std::vector<uint8_t> wire(encoded->GetDataLength());
    encoded->ReadNotMovePt(wire.data(), static_cast<uint32_t>(wire.size()));

    for (auto _ : state) {
        auto in = MakeSingleBlock(wire.size() + 16);
        in->Write(wire.data(), static_cast<uint32_t>(wire.size()));

        std::vector<std::shared_ptr<quic::IPacket>> packets;
        bool ok = quic::DecodePackets(in, packets);
        benchmark::DoNotOptimize(ok);
        benchmark::DoNotOptimize(packets.size());
    }
    state.SetBytesProcessed(state.iterations() * wire.size());
}

// Narrower A/B probe: only charge the *dispatch* work (HeaderFlag + version
// parse + make_shared<InitPacket> + InitPacket::DecodeWithoutCrypto). The
// outer buffer is reset with MoveReadPt instead of being rebuilt, so this
// isolates DecodePackets() overhead from buffer allocation churn.

static void BM_Packet_SinglePacket_DecodeViaDispatch_NoAlloc(benchmark::State& state) {
    const size_t payload_bytes = 256;
    auto frame = MakeCryptoFrame(payload_bytes);
    auto frame_buf = MakeSingleBlock(payload_bytes + 64);
    frame->Encode(frame_buf);
    common::SharedBufferSpan payload(frame_buf->GetSharedReadableSpan());

    quic::InitPacket tx;
    tx.GetHeader()->SetPacketNumberLength(2);
    tx.SetPacketNumber(42);
    tx.SetPayload(payload);

    auto encoded = MakeSingleBlock(payload_bytes + 256);
    tx.Encode(encoded);

    std::vector<uint8_t> wire(encoded->GetDataLength());
    encoded->ReadNotMovePt(wire.data(), static_cast<uint32_t>(wire.size()));

    auto in = MakeSingleBlock(wire.size() + 16);
    uint32_t wsz = static_cast<uint32_t>(wire.size());

    for (auto _ : state) {
        // Rewind the buffer to the start each iteration without re-allocating.
        state.PauseTiming();
        in = MakeSingleBlock(wire.size() + 16);
        in->Write(wire.data(), wsz);
        state.ResumeTiming();

        std::vector<std::shared_ptr<quic::IPacket>> packets;
        bool ok = quic::DecodePackets(in, packets);
        benchmark::DoNotOptimize(ok);
        benchmark::DoNotOptimize(packets.size());
    }
    state.SetBytesProcessed(state.iterations() * wire.size());
}

}  // namespace perf
}  // namespace quicx

// ===========================================================================
// Registration
// ===========================================================================

// Payload sweep chosen to cover common sizes in the wild:
//   64   — CRYPTO handshake chunk / small stream write
//   256  — small DATA frame
//   1200 — near-MTU user data
//   4096 — jumbo / coalesced payload

BENCHMARK(quicx::perf::BM_Packet_InitPacket_EncodeNoCrypto)
    ->Arg(64)->Arg(256)->Arg(1200)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Packet_InitPacket_EncodeWithCrypto)
    ->Arg(64)->Arg(256)->Arg(1200)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Packet_InitPacket_DecodeNoCrypto)
    ->Arg(64)->Arg(256)->Arg(1200)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Packet_InitPacket_DecodeWithCrypto)
    ->Arg(64)->Arg(256)->Arg(1200)->Unit(benchmark::kNanosecond);

BENCHMARK(quicx::perf::BM_Packet_HandshakePacket_EncodeNoCrypto)
    ->Arg(64)->Arg(256)->Arg(1200)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Packet_Rtt1Packet_EncodeNoCrypto)
    ->Arg(64)->Arg(256)->Arg(1200)->Arg(4096)->Unit(benchmark::kNanosecond);

BENCHMARK(quicx::perf::BM_Packet_PacketNumber_Encode)
    ->Arg(0x01)->Arg(0x1234)->Arg(0x123456)->Arg(0x12345678)
    ->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Packet_PacketNumber_DecodeTruncated)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(quicx::perf::BM_Packet_Coalesced_Decode)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Packet_SinglePacket_DecodeViaDispatch)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Packet_SinglePacket_DecodeViaDispatch_NoAlloc)->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();

#else
int main() { return 0; }
#endif
