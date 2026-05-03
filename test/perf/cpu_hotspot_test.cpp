// =============================================================================
// cpu_hotspot_test.cpp - CPU Hotspot Analysis Benchmarks for quicX
// =============================================================================
//
// Standardized CPU profiling scenarios for identifying performance bottlenecks.
// Run with generate_flamegraph.sh for visual analysis.
//
// Build:
//   cmake -B build -DENABLE_PERF_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
//   cmake --build build -j
//
// Usage:
//   # Run all hotspot benchmarks
//   ./build/bin/perf/cpu_hotspot_test
//
//   # Run specific scenario
//   ./build/bin/perf/cpu_hotspot_test --benchmark_filter="Handshake"
//
//   # Generate flame graph
//   ./scripts/perf/generate_flamegraph.sh -d 30 ./build/bin/perf/cpu_hotspot_test
//
// =============================================================================

#if defined(QUICX_ENABLE_BENCHMARKS)

#include <benchmark/benchmark.h>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <unordered_map>

// Common
#include "common/alloter/pool_alloter.h"
#include "common/alloter/pool_block.h"
#include "common/buffer/if_buffer.h"
#include "common/buffer/multi_block_buffer.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

// QUIC frames
#include "quic/frame/ack_frame.h"
#include "quic/frame/stream_frame.h"
#include "quic/frame/frame_decode.h"

// QUIC TLS
#include "quic/crypto/tls/tls_ctx.h"

// HTTP/3 QPACK
#include "http3/qpack/qpack_encoder.h"
#include "http3/qpack/huffman_encoder.h"

namespace quicx {
namespace perf {

// ===========================================================================
// Helpers
// ===========================================================================

static std::shared_ptr<common::IBuffer> MakeBuffer(size_t cap = 64 * 1024) {
    auto pool = std::make_shared<common::BlockMemoryPool>(cap, 128);
    return std::make_shared<common::MultiBlockBuffer>(pool);
}

static std::vector<uint8_t> RandomData(size_t size) {
    std::vector<uint8_t> data(size);
    std::mt19937 gen(42);
    std::uniform_int_distribution<uint8_t> dist(0, 255);
    for (auto& b : data) b = dist(gen);
    return data;
}

// ===========================================================================
// Scenario 1: TLS / Crypto Operations
// ===========================================================================
// These benchmarks isolate cryptographic object creation.

static void BM_CpuHotspot_TlsCtxCreation(benchmark::State& state) {
    for (auto _ : state) {
        auto ctx = std::make_shared<quic::TLSCtx>();
        benchmark::DoNotOptimize(ctx);
    }
}

// ===========================================================================
// Scenario 2: Buffer Operations (Data Processing Path)
// ===========================================================================
// Simulates the data copy/encode path that happens on every packet.

static void BM_CpuHotspot_BufferWriteRead(benchmark::State& state) {
    const size_t payload_size = static_cast<size_t>(state.range(0));
    auto data = RandomData(payload_size);

    for (auto _ : state) {
        auto buf = MakeBuffer(payload_size * 2 + 256);

        // Write (simulates incoming packet data)
        buf->Write(data.data(), static_cast<uint32_t>(data.size()));

        // Read (simulates packet parsing)
        std::vector<uint8_t> out(payload_size);
        buf->Read(out.data(), static_cast<uint32_t>(out.size()));

        benchmark::DoNotOptimize(out.data());
    }
    state.SetBytesProcessed(state.iterations() * payload_size * 2);
}

static void BM_CpuHotspot_BufferEncodeVarInt(benchmark::State& state) {
    auto pool = std::make_shared<common::BlockMemoryPool>(4096, 128);

    for (auto _ : state) {
        auto buf = std::make_shared<common::MultiBlockBuffer>(pool);
        common::BufferEncodeWrapper encoder(buf);
        // Encode a mix of varint sizes (1, 2, 4 byte)
        encoder.EncodeFixedUint8(0x3F);
        encoder.EncodeFixedUint16(0x3FFF);
        encoder.EncodeFixedUint32(0x3FFFFFFF);
        benchmark::DoNotOptimize(encoder.GetDataLength());
    }
    state.SetItemsProcessed(state.iterations() * 3);
}

// ===========================================================================
// Scenario 3: Frame Encoding/Decoding
// ===========================================================================
// Frame operations happen on every packet. This is a hot path.

static void BM_CpuHotspot_AckFrameEncode(benchmark::State& state) {
    for (auto _ : state) {
        auto buf = MakeBuffer(4096);

        // Create and encode an ACK frame
        auto ack = std::make_shared<quic::AckFrame>();
        ack->SetLargestAck(100);
        ack->SetAckDelay(10);
        ack->SetFirstAckRange(5);
        ack->AddAckRange(2, 3);  // gap=2, range=3

        ack->Encode(buf);

        benchmark::DoNotOptimize(buf->GetDataLength());
    }
    state.SetItemsProcessed(state.iterations());
}

static void BM_CpuHotspot_AckFrameDecode(benchmark::State& state) {
    // Pre-encode an ACK frame
    auto encoded_buf = MakeBuffer(4096);
    auto ack = std::make_shared<quic::AckFrame>();
    ack->SetLargestAck(100);
    ack->SetAckDelay(10);
    ack->SetFirstAckRange(5);
    ack->AddAckRange(2, 3);
    ack->Encode(encoded_buf);

    // Read encoded bytes
    std::vector<uint8_t> encoded_data(encoded_buf->GetDataLength());
    encoded_buf->ReadNotMovePt(encoded_data.data(), static_cast<uint32_t>(encoded_data.size()));

    for (auto _ : state) {
        auto buf = MakeBuffer(4096);
        buf->Write(encoded_data.data(), static_cast<uint32_t>(encoded_data.size()));

        std::vector<std::shared_ptr<quic::IFrame>> frames;
        quic::DecodeFrames(buf, frames);

        benchmark::DoNotOptimize(frames.size());
    }
    state.SetItemsProcessed(state.iterations());
}

static void BM_CpuHotspot_StreamFrameEncode(benchmark::State& state) {
    auto payload = RandomData(256);

    for (auto _ : state) {
        auto buf = MakeBuffer(4096);

        auto stream_frame = std::make_shared<quic::StreamFrame>();
        stream_frame->SetStreamID(4);
        stream_frame->SetOffset(0);

        // Create a span for stream data
        auto data_buf = MakeBuffer(512);
        data_buf->Write(payload.data(), static_cast<uint32_t>(payload.size()));
        auto span = data_buf->GetSharedReadableSpan(static_cast<uint32_t>(payload.size()));
        stream_frame->SetData(span);

        stream_frame->Encode(buf);

        benchmark::DoNotOptimize(buf->GetDataLength());
    }
    state.SetItemsProcessed(state.iterations());
}

// ===========================================================================
// Scenario 4: QPACK Header Compression
// ===========================================================================
// Header compression is CPU-intensive for HTTP/3

static void BM_CpuHotspot_QpackEncode(benchmark::State& state) {
    http3::QpackEncoder encoder;

    // Typical HTTP request headers
    std::unordered_map<std::string, std::string> headers = {
        {":method", "GET"},
        {":path", "/api/v1/users?page=1&limit=20"},
        {":scheme", "https"},
        {":authority", "example.com"},
        {"accept", "application/json"},
        {"accept-encoding", "gzip, deflate, br"},
        {"user-agent", "quicX/1.0"},
        {"cache-control", "no-cache"},
        {"content-type", "application/json"},
    };

    for (auto _ : state) {
        auto buf = MakeBuffer(4096);
        encoder.Encode(headers, buf);
        benchmark::DoNotOptimize(buf->GetDataLength());
    }
    state.SetItemsProcessed(state.iterations() * headers.size());
}

static void BM_CpuHotspot_QpackDecode(benchmark::State& state) {
    http3::QpackEncoder encoder;

    // Pre-encode headers
    std::unordered_map<std::string, std::string> headers = {
        {":method", "GET"},
        {":path", "/"},
        {":scheme", "https"},
        {":authority", "example.com"},
        {"accept", "*/*"},
    };

    auto encoded = MakeBuffer(4096);
    encoder.Encode(headers, encoded);

    // Read encoded data
    std::vector<uint8_t> encoded_data(encoded->GetDataLength());
    encoded->ReadNotMovePt(encoded_data.data(), static_cast<uint32_t>(encoded_data.size()));

    for (auto _ : state) {
        auto buf = MakeBuffer(4096);
        buf->Write(encoded_data.data(), static_cast<uint32_t>(encoded_data.size()));

        std::unordered_map<std::string, std::string> decoded;
        encoder.Decode(buf, decoded);

        benchmark::DoNotOptimize(decoded.size());
    }
    state.SetItemsProcessed(state.iterations() * headers.size());
}

// ===========================================================================
// Scenario 5: Huffman Encoding/Decoding
// ===========================================================================

static void BM_CpuHotspot_HuffmanEncode(benchmark::State& state) {
    std::string input = "www.example.com";
    auto& huff = http3::HuffmanEncoder::Instance();

    for (auto _ : state) {
        auto encoded = huff.Encode(input);
        benchmark::DoNotOptimize(encoded.data());
    }
    state.SetBytesProcessed(state.iterations() * input.size());
}

static void BM_CpuHotspot_HuffmanDecode(benchmark::State& state) {
    std::string input = "www.example.com";
    auto& huff = http3::HuffmanEncoder::Instance();
    auto encoded = huff.Encode(input);

    for (auto _ : state) {
        auto decoded = huff.Decode(encoded);
        benchmark::DoNotOptimize(decoded.data());
    }
    state.SetBytesProcessed(state.iterations() * encoded.size());
}

// ===========================================================================
// Scenario 6: Memory Allocation Patterns
// ===========================================================================
// Simulates the allocation patterns seen in real packet processing.

static void BM_CpuHotspot_PoolAllocator(benchmark::State& state) {
    const uint32_t alloc_size = static_cast<uint32_t>(state.range(0));
    auto alloc = common::MakePoolAlloterPtr();

    for (auto _ : state) {
        void* ptr = alloc->Malloc(alloc_size);
        benchmark::DoNotOptimize(ptr);
        alloc->Free(ptr, alloc_size);
    }
    state.SetItemsProcessed(state.iterations());
}

static void BM_CpuHotspot_BlockPoolAllocator(benchmark::State& state) {
    const uint32_t block_size = static_cast<uint32_t>(state.range(0));
    auto pool = common::MakeBlockMemoryPoolPtr(block_size, 128);

    for (auto _ : state) {
        void* ptr = pool->PoolLargeMalloc();
        benchmark::DoNotOptimize(ptr);
        pool->PoolLargeFree(ptr);
    }
    state.SetItemsProcessed(state.iterations());
}

static void BM_CpuHotspot_StdMalloc(benchmark::State& state) {
    const size_t alloc_size = static_cast<size_t>(state.range(0));

    for (auto _ : state) {
        void* ptr = malloc(alloc_size);
        benchmark::DoNotOptimize(ptr);
        free(ptr);
    }
    state.SetItemsProcessed(state.iterations());
}

// ===========================================================================
// Scenario 7: Packet Processing Simulation
// ===========================================================================
// Simulates the full per-packet processing hot path.

static void BM_CpuHotspot_PacketProcessingSimulation(benchmark::State& state) {
    auto pool = std::make_shared<common::BlockMemoryPool>(4096, 128);
    auto data = RandomData(1200);  // Typical QUIC packet size

    for (auto _ : state) {
        // 1. Allocate buffer for incoming packet
        auto buf = std::make_shared<common::MultiBlockBuffer>(pool);

        // 2. Copy packet data into buffer
        buf->Write(data.data(), static_cast<uint32_t>(data.size()));

        // 3. Parse header (read first byte)
        uint8_t first_byte = 0;
        buf->ReadNotMovePt(&first_byte, 1);

        // 4. Simulate frame decoding read
        std::vector<uint8_t> frame_data(64);
        buf->Read(frame_data.data(), static_cast<uint32_t>(frame_data.size()));

        benchmark::DoNotOptimize(first_byte);
        benchmark::DoNotOptimize(frame_data.data());
    }
    state.SetBytesProcessed(state.iterations() * 1200);
}

// ===========================================================================
// Scenario 8: Multi-threaded Contention
// ===========================================================================

static void BM_CpuHotspot_MultiThreadBufferAlloc(benchmark::State& state) {
    const int num_threads = static_cast<int>(state.range(0));
    auto pool = std::make_shared<common::BlockMemoryPool>(4096, 256);

    for (auto _ : state) {
        std::vector<std::thread> threads;
        std::atomic<int> ops{0};

        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&pool, &ops]() {
                for (int j = 0; j < 1000; ++j) {
                    void* ptr = pool->PoolLargeMalloc();
                    benchmark::DoNotOptimize(ptr);
                    pool->PoolLargeFree(ptr);
                    ops.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }
    }
    state.SetItemsProcessed(state.iterations() * num_threads * 1000);
}

}  // namespace perf
}  // namespace quicx

// ===========================================================================
// Register Benchmarks
// ===========================================================================

// Scenario 1: TLS/Crypto
BENCHMARK(quicx::perf::BM_CpuHotspot_TlsCtxCreation);

// Scenario 2: Buffer operations
BENCHMARK(quicx::perf::BM_CpuHotspot_BufferWriteRead)
    ->Arg(64)->Arg(256)->Arg(1200)->Arg(4096)->Arg(16384)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(quicx::perf::BM_CpuHotspot_BufferEncodeVarInt);

// Scenario 3: Frame encode/decode
BENCHMARK(quicx::perf::BM_CpuHotspot_AckFrameEncode);
BENCHMARK(quicx::perf::BM_CpuHotspot_AckFrameDecode);
BENCHMARK(quicx::perf::BM_CpuHotspot_StreamFrameEncode);

// Scenario 4: QPACK
BENCHMARK(quicx::perf::BM_CpuHotspot_QpackEncode);
BENCHMARK(quicx::perf::BM_CpuHotspot_QpackDecode);

// Scenario 5: Huffman
BENCHMARK(quicx::perf::BM_CpuHotspot_HuffmanEncode);
BENCHMARK(quicx::perf::BM_CpuHotspot_HuffmanDecode);

// Scenario 6: Memory allocation comparison
BENCHMARK(quicx::perf::BM_CpuHotspot_PoolAllocator)
    ->Arg(16)->Arg(64)->Arg(128)->Arg(256);
BENCHMARK(quicx::perf::BM_CpuHotspot_BlockPoolAllocator)
    ->Arg(1024)->Arg(2048)->Arg(4096)->Arg(16384);
BENCHMARK(quicx::perf::BM_CpuHotspot_StdMalloc)
    ->Arg(16)->Arg(64)->Arg(128)->Arg(256)->Arg(1024)->Arg(4096);

// Scenario 7: Full packet processing
BENCHMARK(quicx::perf::BM_CpuHotspot_PacketProcessingSimulation)
    ->Unit(benchmark::kMicrosecond);

// Scenario 8: Multi-threaded contention
BENCHMARK(quicx::perf::BM_CpuHotspot_MultiThreadBufferAlloc)
    ->Arg(1)->Arg(2)->Arg(4)->Arg(8);

BENCHMARK_MAIN();

#else
int main() {
    return 0;
}
#endif
