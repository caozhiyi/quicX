// =============================================================================
// profile_qpack_encode.cpp - Targeted CPU profile of QpackEncoder::Encode().
//
// Rationale:
//   BM_Qpack_Encode_LargeHeaders/0 (11 headers, no cookies, static table only)
//   takes ~9.45 us - that is ~860 ns per header, while mature HPACK/QPACK
//   encoders routinely stay below 100 ns/header.  Reading source alone cannot
//   confidently tell us whether the cost is in Huffman, the static-table
//   lookup, std::string allocations, or prefixed-integer encoding.
//
// This driver runs QpackEncoder::Encode() in a tight loop under a SIGPROF
// sampler so that flamegraph-ready data can be produced via
// test/perf/tools/resolve_stacks.py.
// =============================================================================
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/alloter/pool_block.h"
#include "common/buffer/multi_block_buffer.h"
#include "common/log/log.h"

#include "http3/qpack/qpack_encoder.h"

#include "test/perf/tools/sampling_profiler.h"

using namespace quicx;

static std::shared_ptr<common::IBuffer> MakeBuffer(size_t cap = 16 * 1024) {
    auto pool = std::make_shared<common::BlockMemoryPool>(cap, 8);
    return std::make_shared<common::MultiBlockBuffer>(pool);
}

static std::unordered_map<std::string, std::string> MakeTypicalRequestHeaders() {
    return {
        {":method", "GET"},
        {":path", "/api/v1/users?page=1&limit=20"},
        {":scheme", "https"},
        {":authority", "example.com"},
        {"accept", "application/json"},
        {"accept-encoding", "gzip, deflate, br"},
        {"user-agent", "quicX/1.0 benchmark"},
        {"cache-control", "no-cache"},
        {"content-type", "application/json"},
    };
}

static std::unordered_map<std::string, std::string> MakeLargeRequestHeaders(int n_cookies) {
    auto m = MakeTypicalRequestHeaders();
    m[":path"] =
        "/service/v3/users/0123456789abcdef/orders"
        "?fields=id,name,email,address,phone,created_at"
        "&filter=status:active&sort=-created_at&page=1&limit=100";
    m["authorization"] =
        "Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
        "eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IlF1aWNYIn0."
        "very-long-signature-string-that-does-not-matter-for-this-benchmark";
    std::string cookies;
    for (int i = 0; i < n_cookies; ++i) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "sk_%d=abcdef0123456789_%d;", i, i);
        cookies.append(buf);
    }
    m["cookie"] = cookies;
    return m;
}

int main(int argc, char** argv) {
    int hz = 997;
    int seconds = 5;
    int n_cookies = 0;
    const char* out = "/tmp/qpack_encode_stacks.raw";
    bool mute_log = true;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--hz") && i + 1 < argc) hz = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--cookies") && i + 1 < argc) n_cookies = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!std::strcmp(argv[i], "--keep-log")) mute_log = false;
    }
    if (mute_log) {
        common::LOG_SET_LEVEL(common::LogLevel::kNull);
    }

    auto headers = MakeLargeRequestHeaders(n_cookies);
    std::fprintf(stderr, "profile_qpack_encode: %zu headers (cookies=%d)\n",
                 headers.size(), n_cookies);

    // Dump /proc/self/maps for the offline resolver.
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

    perf::SamplingProfiler prof(out, hz);
    prof.Start();

    auto t0 = std::chrono::steady_clock::now();
    uint64_t iters = 0;
    const auto deadline = t0 + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        // Fresh encoder per batch so the dynamic table does not grow beyond
        // the request pattern we want to profile.
        http3::QpackEncoder encoder;
        for (int k = 0; k < 64; ++k) {
            auto buf = MakeBuffer();
            bool ok = encoder.Encode(headers, buf);
            (void) ok;
            ++iters;
        }
    }
    prof.Stop();
    auto t1 = std::chrono::steady_clock::now();
    double elapsed_s = std::chrono::duration<double>(t1 - t0).count();
    std::fprintf(stderr, "encoded %lu calls in %.3f s  => %.1f ns/call  (samples=%u)\n",
                 (unsigned long)iters, elapsed_s, elapsed_s * 1e9 / iters,
                 prof.SampleCount());

    prof.Dump();
    return 0;
}
