// =============================================================================
// qpack_perf_test.cpp - QPACK depth benchmarks
// =============================================================================
//
// The existing cpu_hotspot_test runs QPACK Encode/Decode once on a fixed
// 9-header request. Real HTTP/3 servers see a wider set of patterns:
//   - Large header sets (many cookies, long paths)
//   - Dynamic table insert / lookup / eviction churn
//   - Blocked-stream bookkeeping (QpackBlockedRegistry Ack / Remove)
//
// This suite covers those cases so we have baselines for HTTP/3 CPU work.
//
// =============================================================================

#if defined(QUICX_ENABLE_BENCHMARKS)

#include <benchmark/benchmark.h>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/alloter/pool_block.h"
#include "common/buffer/multi_block_buffer.h"

#include "http3/qpack/blocked_registry.h"
#include "http3/qpack/dynamic_table.h"
#include "http3/qpack/qpack_encoder.h"

namespace quicx {
namespace perf {

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

    // Long path (simulate a complex REST call).
    m[":path"] =
        "/service/v3/users/0123456789abcdef/orders"
        "?fields=id,name,email,address,phone,created_at"
        "&filter=status:active&sort=-created_at&page=1&limit=100";

    // Long auth.
    m["authorization"] =
        "Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
        "eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IlF1aWNYIn0."
        "very-long-signature-string-that-does-not-matter-for-this-benchmark";

    // Many cookie entries (tend to hit Huffman + literal encoding heavily).
    std::string cookies;
    for (int i = 0; i < n_cookies; ++i) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "sk_%d=abcdef0123456789_%d;", i, i);
        cookies.append(buf);
    }
    m["cookie"] = cookies;
    return m;
}

// ===========================================================================
// Scenario 1: Encode / Decode at varying header set sizes
// ===========================================================================

static void BM_Qpack_Encode_LargeHeaders(benchmark::State& state) {
    const int n_cookies = static_cast<int>(state.range(0));
    auto headers = MakeLargeRequestHeaders(n_cookies);

    http3::QpackEncoder encoder;

    for (auto _ : state) {
        auto buf = MakeBuffer();
        bool ok = encoder.Encode(headers, buf);
        benchmark::DoNotOptimize(ok);
        benchmark::DoNotOptimize(buf->GetDataLength());
    }
    state.SetItemsProcessed(state.iterations() * headers.size());
    state.counters["n_headers"] = static_cast<double>(headers.size());
}

static void BM_Qpack_Decode_LargeHeaders(benchmark::State& state) {
    const int n_cookies = static_cast<int>(state.range(0));
    auto headers = MakeLargeRequestHeaders(n_cookies);

    http3::QpackEncoder encoder;
    auto encoded = MakeBuffer();
    if (!encoder.Encode(headers, encoded)) {
        state.SkipWithError("encode failed");
        return;
    }

    std::vector<uint8_t> wire(encoded->GetDataLength());
    encoded->ReadNotMovePt(wire.data(), static_cast<uint32_t>(wire.size()));

    for (auto _ : state) {
        auto buf = MakeBuffer(wire.size() + 256);
        buf->Write(wire.data(), static_cast<uint32_t>(wire.size()));

        http3::QpackEncoder dec;  // fresh decoder so dynamic table state is clean
        std::unordered_map<std::string, std::string> out;
        bool ok = dec.Decode(buf, out);
        benchmark::DoNotOptimize(ok);
        benchmark::DoNotOptimize(out.size());
    }
    state.SetItemsProcessed(state.iterations() * headers.size());
    state.counters["n_headers"] = static_cast<double>(headers.size());
    state.counters["wire_B"] = static_cast<double>(wire.size());
}

// ===========================================================================
// Scenario 2: Dynamic-table insert + find + eviction
// ===========================================================================

static void BM_Qpack_DynamicTable_Insert(benchmark::State& state) {
    // Small table forces eviction on every few inserts.
    const uint32_t cap = static_cast<uint32_t>(state.range(0));

    for (auto _ : state) {
        http3::DynamicTable table(cap);
        for (int i = 0; i < 64; ++i) {
            char name[32];
            char value[32];
            std::snprintf(name, sizeof(name), "x-hdr-%d", i);
            std::snprintf(value, sizeof(value), "val-%d-%d", i, i * 3);
            bool ok = table.AddHeaderItem(name, value);
            benchmark::DoNotOptimize(ok);
        }
        benchmark::DoNotOptimize(table.GetEntryCount());
    }
    state.SetItemsProcessed(state.iterations() * 64);
    state.counters["cap_B"] = static_cast<double>(cap);
}

static void BM_Qpack_DynamicTable_Find(benchmark::State& state) {
    // Populate a sizable dynamic table, then measure lookup.
    http3::DynamicTable table(64 * 1024);
    for (int i = 0; i < 200; ++i) {
        char name[32];
        char value[32];
        std::snprintf(name, sizeof(name), "x-hdr-%d", i);
        std::snprintf(value, sizeof(value), "val-%d", i);
        table.AddHeaderItem(name, value);
    }

    int probe = 0;
    for (auto _ : state) {
        char name[32];
        char value[32];
        std::snprintf(name, sizeof(name), "x-hdr-%d", probe % 200);
        std::snprintf(value, sizeof(value), "val-%d", probe % 200);
        int32_t idx = table.FindHeaderItemIndex(name, value);
        int64_t abs_idx = table.FindAbsoluteIndex(name, value);
        benchmark::DoNotOptimize(idx);
        benchmark::DoNotOptimize(abs_idx);
        ++probe;
    }
    state.SetItemsProcessed(state.iterations());
}

// ===========================================================================
// Scenario 3: QpackBlockedRegistry (blocked-streams fast path)
// ===========================================================================
//
// Simulates many concurrent blocked header sections being Ack'd and removed.
// Exercises the recently-fixed AckByStreamId / RemoveByStreamId paths.

static void BM_Qpack_BlockedRegistry_AddAck(benchmark::State& state) {
    const int n_pending = static_cast<int>(state.range(0));

    for (auto _ : state) {
        http3::QpackBlockedRegistry reg;
        reg.SetMaxBlockedStreams(/*unlimited for the test*/ 0);

        // Add N pending sections on distinct streams, with a retry closure.
        for (int i = 0; i < n_pending; ++i) {
            uint64_t stream_id = static_cast<uint64_t>(i);
            // Key encodes (stream_id << 32) | section_no (0 here).
            uint64_t key = (stream_id << 32);
            reg.Add(key, []() {});
        }
        benchmark::DoNotOptimize(reg.GetBlockedCount());

        // Ack every other stream id; remove the rest.
        for (int i = 0; i < n_pending; ++i) {
            uint64_t stream_id = static_cast<uint64_t>(i);
            if (i % 2 == 0) {
                reg.AckByStreamId(stream_id);
            } else {
                reg.RemoveByStreamId(stream_id);
            }
        }
        benchmark::DoNotOptimize(reg.GetBlockedCount());
    }
    state.SetItemsProcessed(state.iterations() * n_pending * 2);  // add + ack/remove
}

}  // namespace perf
}  // namespace quicx

// ===========================================================================
// Registration
// ===========================================================================

BENCHMARK(quicx::perf::BM_Qpack_Encode_LargeHeaders)
    ->Arg(0)->Arg(8)->Arg(32)->Arg(64)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(quicx::perf::BM_Qpack_Decode_LargeHeaders)
    ->Arg(0)->Arg(8)->Arg(32)->Arg(64)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK(quicx::perf::BM_Qpack_DynamicTable_Insert)
    ->Arg(512)->Arg(4096)->Arg(16384)
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(quicx::perf::BM_Qpack_DynamicTable_Find)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(quicx::perf::BM_Qpack_BlockedRegistry_AddAck)
    ->Arg(16)->Arg(128)->Arg(1024)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();

#else
int main() { return 0; }
#endif
