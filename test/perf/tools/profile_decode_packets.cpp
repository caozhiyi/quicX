// =============================================================================
// profile_decode_packets.cpp - Targeted CPU profile of quic::DecodePackets().
//
// Rationale:
//   The benchmark BM_Packet_SinglePacket_DecodeViaDispatch runs DecodePackets
//   at ~65 us/call for a 256 B InitPacket while the "manual fast path"
//   (DecodeFlag + InitPacket::DecodeWithoutCrypto) only takes ~330 ns.
//   That is a ~200x gap that is not explained by the source code, so we need
//   actual sampling data.
//
// What this driver does:
//   1. Build one InitPacket on the wire (single packet, no coalescing).
//   2. Loop DecodePackets() for a fixed wall-time while the SIGPROF sampler
//      records stacks.
//   3. Write raw "addr;addr;..." samples to /tmp/decode_stacks.raw.
//   4. An offline Python resolver (test/perf/tools/resolve_stacks.py)
//      symbolizes with addr2line + c++filt and produces a flamegraph-ready
//      "collapsed" file.
// =============================================================================
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"
#include "common/log/log.h"

#include "quic/frame/crypto_frame.h"
#include "quic/crypto/type.h"
#include "quic/common/version.h"
#include "quic/packet/init_packet.h"
#include "quic/packet/packet_decode.h"

#include "test/perf/tools/sampling_profiler.h"

using namespace quicx;

static std::shared_ptr<common::SingleBlockBuffer> MakeBuf(size_t cap) {
    return std::make_shared<common::SingleBlockBuffer>(
        std::make_shared<common::StandaloneBufferChunk>(cap));
}

int main(int argc, char** argv) {
    int hz = 997;
    int seconds = 5;
    const char* out = "/tmp/decode_stacks.raw";
    bool mute_log = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--hz") && i + 1 < argc) hz = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!std::strcmp(argv[i], "--mute-log")) mute_log = true;
    }
    if (mute_log) {
        LOG_SET_LEVEL(common::LogLevel::kNull);
    }

    // ---- Build one InitPacket on the wire ----
    const size_t kPayloadBytes = 256;
    std::vector<uint8_t> data(kPayloadBytes, 0x5a);
    auto data_buf = MakeBuf(kPayloadBytes + 32);
    data_buf->Write(data.data(), static_cast<uint32_t>(data.size()));

    auto crypto = std::make_shared<quic::CryptoFrame>();
    crypto->SetOffset(0);
    crypto->SetEncryptionLevel(quic::PacketCryptoLevel::kInitialCryptoLevel);
    crypto->SetData(data_buf->GetSharedReadableSpan());

    auto frame_buf = MakeBuf(kPayloadBytes + 64);
    crypto->Encode(frame_buf);
    common::SharedBufferSpan payload(frame_buf->GetSharedReadableSpan());

    quic::InitPacket tx;
    static_cast<quic::LongHeader*>(tx.GetHeader())->SetVersion(quic::GetDefaultVersion());
    tx.GetHeader()->SetPacketNumberLength(2);
    tx.SetPacketNumber(42);
    tx.SetPayload(payload);

    auto encoded = MakeBuf(kPayloadBytes + 256);
    if (!tx.Encode(encoded)) { std::fprintf(stderr, "encode failed\n"); return 1; }

    std::vector<uint8_t> wire(encoded->GetDataLength());
    encoded->ReadNotMovePt(wire.data(), static_cast<uint32_t>(wire.size()));
    std::fprintf(stderr, "profile_decode_packets: wire size = %zu bytes\n", wire.size());

    // Dump /proc/self/maps alongside the raw samples so the offline resolver
    // can translate runtime PCs to (binary, offset).
    {
        std::string maps_path = std::string(out) + ".maps";
        FILE* mfp = std::fopen(maps_path.c_str(), "w");
        if (mfp) {
            FILE* self = std::fopen("/proc/self/maps", "r");
            if (self) {
                char buf[512];
                while (std::fgets(buf, sizeof(buf), self)) std::fputs(buf, mfp);
                std::fclose(self);
            }
            std::fclose(mfp);
        }
    }

    // ---- run under sampler ----
    perf::SamplingProfiler prof(out, hz);
    prof.Start();

    auto t0 = std::chrono::steady_clock::now();
    uint64_t iters = 0;
    const auto deadline = t0 + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        for (int k = 0; k < 64; ++k) {
            auto in = MakeBuf(wire.size() + 16);
            in->Write(wire.data(), static_cast<uint32_t>(wire.size()));
            std::vector<std::shared_ptr<quic::IPacket>> packets;
            (void) quic::DecodePackets(in, packets);
            ++iters;
        }
    }
    prof.Stop();
    auto t1 = std::chrono::steady_clock::now();
    double elapsed_s = std::chrono::duration<double>(t1 - t0).count();
    std::fprintf(stderr, "decoded %lu packets in %.3f s  => %.1f ns/call  (samples=%u)\n",
                 (unsigned long)iters, elapsed_s, elapsed_s * 1e9 / iters,
                 prof.SampleCount());

    prof.Dump();
    return 0;
}
