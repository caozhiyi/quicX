// =============================================================================
// memory_pool_efficiency_test.cpp - Memory Pool Efficiency Analysis for quicX
// =============================================================================
//
// Analyzes the efficiency of quicX's custom memory pool allocators:
//   - PoolAlloter: Small object pool (slab allocator, <= 256 bytes)
//   - BlockMemoryPool: Large block pool (configurable block size)
//
// Reports:
//   - Allocation throughput vs std::malloc
//   - Tail latency (p50/p99/p999/max) for each allocator
//   - Fragmentation / pool water-mark after mixed workloads
//   - Cross-thread contention overhead (per-thread pool ownership)
//   - "Per-connection pool" scenarios with realistic frame allocation
//   - PoolAlloter thread-safety contract: single-owner, pinned to one thread
//     (connection-affine). This mirrors quicX's Connection threading model.
//
// Build:
//   cmake -B build -DENABLE_PERF_TESTS=ON && cmake --build build -j
//
// Usage:
//   ./build/bin/perf/memory_pool_efficiency_test
//   ./build/bin/perf/memory_pool_efficiency_test --benchmark_format=json > pool_report.json
//
// =============================================================================

#if defined(QUICX_ENABLE_BENCHMARKS)

#include <benchmark/benchmark.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <numeric>
#include <random>
#include <thread>
#include <utility>
#include <vector>

#include "common/alloter/pool_alloter.h"
#include "common/alloter/pool_block.h"
#include "common/alloter/normal_alloter.h"
#include "common/alloter/if_alloter.h"
#include "common/buffer/if_buffer.h"
#include "common/buffer/multi_block_buffer.h"

#include "quic/frame/stream_frame.h"
#include "quic/frame/ack_frame.h"

namespace quicx {
namespace perf {

// ===========================================================================
// Helpers
// ===========================================================================

// Per-benchmark RNG: seeded once per Run from a fixed seed so the random
// stream is reproducible across benchmark invocations (the previous
// module-global mt19937 was shared by every case and produced unstable
// numbers depending on run order).
static std::mt19937 MakeRng(uint64_t seed = 42) {
    return std::mt19937(static_cast<uint32_t>(seed));
}

// ---------------------------------------------------------------------------
// Latency-sample helper: record per-op wall-time samples and expose
// p50/p99/p999/max on state.counters. Use sparingly -- recording adds
// overhead, so we only enable it for specific "latency" benchmarks.
// ---------------------------------------------------------------------------
struct LatencyRecorder {
    std::vector<uint64_t> samples_ns;  // pre-reserved, never reallocated in loop

    explicit LatencyRecorder(size_t reserve) {
        samples_ns.reserve(reserve);
    }

    inline std::chrono::steady_clock::time_point Start() {
        return std::chrono::steady_clock::now();
    }

    inline void Stop(std::chrono::steady_clock::time_point t0) {
        auto t1 = std::chrono::steady_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        samples_ns.push_back(static_cast<uint64_t>(ns));
    }

    void Report(benchmark::State& state) {
        if (samples_ns.empty()) return;
        std::sort(samples_ns.begin(), samples_ns.end());
        auto pick = [&](double q) -> double {
            size_t idx = static_cast<size_t>(q * (samples_ns.size() - 1));
            return static_cast<double>(samples_ns[idx]);
        };
        state.counters["p50_ns"]  = pick(0.50);
        state.counters["p99_ns"]  = pick(0.99);
        state.counters["p999_ns"] = pick(0.999);
        state.counters["max_ns"]  = static_cast<double>(samples_ns.back());
    }
};

// ===========================================================================
// 1. PoolAlloter vs std::malloc throughput (single-size hot path)
// ===========================================================================

static void BM_PoolEfficiency_PoolAlloterVsMalloc_Pool(benchmark::State& state) {
    const uint32_t size = static_cast<uint32_t>(state.range(0));
    auto alloc = common::MakePoolAlloterPtr();

    for (auto _ : state) {
        void* ptr = alloc->Malloc(size);
        benchmark::DoNotOptimize(ptr);
        alloc->Free(ptr, size);
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("PoolAlloter");
}

static void BM_PoolEfficiency_PoolAlloterVsMalloc_Malloc(benchmark::State& state) {
    const size_t size = static_cast<size_t>(state.range(0));

    for (auto _ : state) {
        void* ptr = malloc(size);
        benchmark::DoNotOptimize(ptr);
        free(ptr);
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("std::malloc");
}

// ===========================================================================
// 2. BlockMemoryPool vs std::malloc (large block path)
// ===========================================================================

static void BM_PoolEfficiency_BlockPoolVsMalloc_Pool(benchmark::State& state) {
    const uint32_t block_size = static_cast<uint32_t>(state.range(0));
    auto pool = common::MakeBlockMemoryPoolPtr(block_size, 128);

    for (auto _ : state) {
        void* ptr = pool->PoolLargeMalloc();
        benchmark::DoNotOptimize(ptr);
        pool->PoolLargeFree(ptr);
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("BlockMemoryPool");
}

static void BM_PoolEfficiency_BlockPoolVsMalloc_Malloc(benchmark::State& state) {
    const size_t block_size = static_cast<size_t>(state.range(0));

    for (auto _ : state) {
        void* ptr = malloc(block_size);
        benchmark::DoNotOptimize(ptr);
        free(ptr);
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("std::malloc");
}

// ===========================================================================
// 3. Mixed allocation workload (realistic pattern)
//    Fix: use swap+pop (O(1)) instead of vector::erase(O(n)) so we don't end
//    up benchmarking std::vector memmove, and use a local RNG for stable
//    reproducibility.
// ===========================================================================

static void BM_PoolEfficiency_MixedWorkload_Pool(benchmark::State& state) {
    auto alloc = common::MakePoolAlloterPtr();
    std::uniform_int_distribution<uint32_t> size_dist(8, 256);
    auto rng = MakeRng();

    for (auto _ : state) {
        std::vector<std::pair<void*, uint32_t>> ptrs;
        ptrs.reserve(16);

        for (int i = 0; i < 10; ++i) {
            uint32_t sz = size_dist(rng);
            void* ptr = alloc->Malloc(sz);
            ptrs.push_back({ptr, sz});
        }

        for (int i = 0; i < 7 && !ptrs.empty(); ++i) {
            size_t idx = rng() % ptrs.size();
            alloc->Free(ptrs[idx].first, ptrs[idx].second);
            // O(1) unordered removal
            ptrs[idx] = ptrs.back();
            ptrs.pop_back();
        }

        for (int i = 0; i < 5; ++i) {
            uint32_t sz = size_dist(rng);
            void* ptr = alloc->Malloc(sz);
            ptrs.push_back({ptr, sz});
        }

        for (auto& kv : ptrs) {
            alloc->Free(kv.first, kv.second);
        }
    }
    state.SetItemsProcessed(state.iterations() * 22);
}

static void BM_PoolEfficiency_MixedWorkload_Malloc(benchmark::State& state) {
    std::uniform_int_distribution<size_t> size_dist(8, 256);
    auto rng = MakeRng();

    for (auto _ : state) {
        std::vector<std::pair<void*, size_t>> ptrs;
        ptrs.reserve(16);

        for (int i = 0; i < 10; ++i) {
            size_t sz = size_dist(rng);
            void* ptr = malloc(sz);
            ptrs.push_back({ptr, sz});
        }

        for (int i = 0; i < 7 && !ptrs.empty(); ++i) {
            size_t idx = rng() % ptrs.size();
            free(ptrs[idx].first);
            ptrs[idx] = ptrs.back();
            ptrs.pop_back();
        }

        for (int i = 0; i < 5; ++i) {
            size_t sz = size_dist(rng);
            void* ptr = malloc(sz);
            ptrs.push_back({ptr, sz});
        }

        for (auto& kv : ptrs) {
            free(kv.first);
        }
    }
    state.SetItemsProcessed(state.iterations() * 22);
}

// ===========================================================================
// 4. Buffer allocation pattern (per-packet simulation)
//    Fix: the previous version used std::make_shared<MultiBlockBuffer> inside
//    the timed loop, so the control-block + buffer object allocation
//    dominated the numbers. We now reuse a single long-lived buffer object
//    and only exercise the pool's Malloc/Free by calling Clear() between
//    packets (same chunks, same read/write counters, no per-iteration
//    make_shared).
// ===========================================================================

static void BM_PoolEfficiency_BufferPerPacket(benchmark::State& state) {
    const int packets = static_cast<int>(state.range(0));
    auto pool = std::make_shared<common::BlockMemoryPool>(4096, 64);
    auto buf  = std::make_shared<common::MultiBlockBuffer>(pool);

    std::vector<uint8_t> data(1200, 0xAB);
    std::vector<uint8_t> header(32);
    std::vector<uint8_t> payload(1168);

    for (auto _ : state) {
        for (int i = 0; i < packets; ++i) {
            buf->Write(data.data(), static_cast<uint32_t>(data.size()));
            buf->Read(header.data(),  static_cast<uint32_t>(header.size()));
            buf->Read(payload.data(), static_cast<uint32_t>(payload.size()));
            benchmark::DoNotOptimize(header.data());
            benchmark::DoNotOptimize(payload.data());
        }
        // Return chunks back to the BlockMemoryPool each round so we actually
        // exercise PoolLargeMalloc/PoolLargeFree, not just the per-chunk
        // write/read pointers.
        buf->Clear();
    }
    state.SetItemsProcessed(state.iterations() * packets);
}

// ===========================================================================
// 5. Pool Expansion behavior
//    Fix: move the Free() loop outside of the timed region so we report only
//    expansion cost, not destruction cost. state.counters are only kept from
//    the last iteration by Google Benchmark anyway, but using PauseTiming
//    makes the intent explicit.
// ===========================================================================

static void BM_PoolEfficiency_PoolExpansion(benchmark::State& state) {
    const int num_blocks = static_cast<int>(state.range(0));
    const uint32_t block_size = 4096;

    for (auto _ : state) {
        auto pool = common::MakeBlockMemoryPoolPtr(block_size, 4);

        std::vector<void*> ptrs;
        ptrs.reserve(num_blocks);

        for (int i = 0; i < num_blocks; ++i) {
            ptrs.push_back(pool->PoolLargeMalloc());
        }

        state.counters["final_pool_size"] = benchmark::Counter(
            static_cast<double>(pool->GetSize()));

        state.PauseTiming();
        for (auto& ptr : ptrs) {
            pool->PoolLargeFree(ptr);
        }
        state.counters["after_free_pool_size"] = benchmark::Counter(
            static_cast<double>(pool->GetSize()));
        state.ResumeTiming();
    }
}

// ===========================================================================
// 6. Multi-threaded pool contention (per-thread ownership model)
// ===========================================================================
// BlockMemoryPool is intentionally NOT internally synchronised; it is
// designed around single-thread-per-pool ownership with cross-thread frees
// being deferred via IEventLoop::RunInLoop (see pool_block.cpp). Calling
// PoolLargeMalloc/PoolLargeFree on a shared pool from multiple threads
// without an event loop races on the internal free_mem_vec_ and, in
// practice, triggers glibc "double free or corruption" on shutdown.
//
// For a multi-threaded throughput benchmark we therefore give each worker
// its own pool. This matches real deployment (one pool per I/O thread /
// event loop) and measures the allocator fast-path under concurrency
// without violating the pool's threading contract.
static void BM_PoolEfficiency_MultiThreadContention(benchmark::State& state) {
    const int num_threads = static_cast<int>(state.range(0));
    const int ops_per_thread = 10000;

    for (auto _ : state) {
        std::atomic<uint64_t> total_ops{0};
        std::vector<std::thread> threads;
        threads.reserve(num_threads);

        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&total_ops, ops_per_thread]() {
                auto pool = common::MakeBlockMemoryPoolPtr(4096, 256);
                for (int i = 0; i < ops_per_thread; ++i) {
                    void* ptr = pool->PoolLargeMalloc();
                    benchmark::DoNotOptimize(ptr);
                    pool->PoolLargeFree(ptr);
                    total_ops.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        for (auto& t : threads) t.join();

        state.counters["total_ops"] = benchmark::Counter(
            static_cast<double>(total_ops.load()));
    }
    state.SetItemsProcessed(state.iterations() * num_threads * ops_per_thread);
}

// ===========================================================================
// 7. Allocation size distribution (real-world simulation)
// ===========================================================================

static void BM_PoolEfficiency_RealWorldSizeDistribution(benchmark::State& state) {
    auto alloc = common::MakePoolAlloterPtr();

    // Typical quicX allocation sizes:
    // - Frame headers: 8-32 bytes
    // - Short headers: 24-48 bytes
    // - Stream data chunks: 128-256 bytes
    // - Connection state: 200+ bytes
    const std::vector<uint32_t> typical_sizes = {
        8, 16, 24, 32, 48, 64, 96, 128, 192, 256,
        8, 16, 24, 32, 16, 24, 32, 48, 64, 128,
    };

    for (auto _ : state) {
        std::vector<std::pair<void*, uint32_t>> ptrs;
        ptrs.reserve(typical_sizes.size());

        for (uint32_t sz : typical_sizes) {
            void* ptr = alloc->Malloc(sz);
            ptrs.push_back({ptr, sz});
        }

        for (auto& kv : ptrs) {
            alloc->Free(kv.first, kv.second);
        }
    }
    state.SetItemsProcessed(state.iterations() * typical_sizes.size() * 2);
}

// ===========================================================================
// 8. NormalAlloter baseline (direct malloc/free wrapper)
// ===========================================================================

static void BM_PoolEfficiency_NormalAlloter(benchmark::State& state) {
    const uint32_t size = static_cast<uint32_t>(state.range(0));
    auto alloc = common::MakeNormalAlloterPtr();

    for (auto _ : state) {
        void* ptr = alloc->Malloc(size);
        benchmark::DoNotOptimize(ptr);
        alloc->Free(ptr);
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("NormalAlloter");
}

// ===========================================================================
// 9. kDefaultMaxBytes boundary -- pool fall-through to NormalAlloter
// ---------------------------------------------------------------------------
// Important edge case: once size > 256B the pool path degrades to a plain
// malloc call (and loses free-list reuse). This micro-bench confirms the
// degradation is not worse than raw malloc (i.e. the indirection is cheap).
// ===========================================================================

static void BM_PoolEfficiency_PoolFallthrough_Large(benchmark::State& state) {
    const uint32_t size = static_cast<uint32_t>(state.range(0));
    auto alloc = common::MakePoolAlloterPtr();

    for (auto _ : state) {
        void* ptr = alloc->Malloc(size);
        benchmark::DoNotOptimize(ptr);
        alloc->Free(ptr, size);
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("PoolAlloter(>256B fallthrough)");
}

// ===========================================================================
// 10. Tail-latency comparison (Pool vs malloc) -- p50 / p99 / p999 / max
// ---------------------------------------------------------------------------
// Mean throughput under-sells the biggest benefit of an arena allocator:
// elimination of malloc's tail spikes (mmap, arena expansion, glibc lock).
// We sample each alloc+free pair and report the quantiles.
// ===========================================================================

static void BM_PoolEfficiency_Latency_Pool(benchmark::State& state) {
    const uint32_t size = 64;
    auto alloc = common::MakePoolAlloterPtr();
    LatencyRecorder rec(static_cast<size_t>(state.max_iterations));

    for (auto _ : state) {
        auto t0 = rec.Start();
        void* ptr = alloc->Malloc(size);
        benchmark::DoNotOptimize(ptr);
        alloc->Free(ptr, size);
        rec.Stop(t0);
    }
    rec.Report(state);
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("PoolAlloter");
}

static void BM_PoolEfficiency_Latency_Malloc(benchmark::State& state) {
    const size_t size = 64;
    LatencyRecorder rec(static_cast<size_t>(state.max_iterations));

    for (auto _ : state) {
        auto t0 = rec.Start();
        void* ptr = malloc(size);
        benchmark::DoNotOptimize(ptr);
        free(ptr);
        rec.Stop(t0);
    }
    rec.Report(state);
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("std::malloc");
}

// ===========================================================================
// 11. Real-type: quic::StreamFrame - Pool vs make_shared
// ---------------------------------------------------------------------------
// Gap 4 closes the largest missing piece: the microbench numbers say nothing
// about whether the user-facing API (PoolNewSharePtr / PoolNew) actually
// beats make_shared for real frame types. We measure three variants for
// sizeof(StreamFrame) objects:
//   a) std::make_shared<StreamFrame>()               -- baseline
//   b) PoolNew<StreamFrame>() + PoolDelete (raw ptr) -- pool + no shared_ptr
//   c) PoolNewSharePtr<StreamFrame>()                -- pool + shared_ptr
//
// The interesting finding is typically that (c) may lose most of the pool
// benefit because the control block is still allocated via the global new,
// and the custom deleter captures a shared_ptr<IAlloter> (extra atomics).
// ===========================================================================

static void BM_PoolEfficiency_StreamFrame_MakeShared(benchmark::State& state) {
    for (auto _ : state) {
        auto f = std::make_shared<quic::StreamFrame>();
        f->SetStreamID(4);
        f->SetOffset(0);
        benchmark::DoNotOptimize(f);
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("make_shared<StreamFrame>");
}

static void BM_PoolEfficiency_StreamFrame_PoolRaw(benchmark::State& state) {
    auto alloter = common::MakePoolAlloterPtr();
    common::AlloterWrap wrap(alloter);

    for (auto _ : state) {
        auto* f = wrap.PoolNew<quic::StreamFrame>();
        f->SetStreamID(4);
        f->SetOffset(0);
        benchmark::DoNotOptimize(f);
        wrap.PoolDelete(f);
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("PoolNew<StreamFrame> + PoolDelete");
}

static void BM_PoolEfficiency_StreamFrame_PoolSharePtr(benchmark::State& state) {
    auto alloter = common::MakePoolAlloterPtr();
    common::AlloterWrap wrap(alloter);

    for (auto _ : state) {
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
        auto f = wrap.PoolNewSharePtr<quic::StreamFrame>();
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif
        f->SetStreamID(4);
        f->SetOffset(0);
        benchmark::DoNotOptimize(f);
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("PoolNewSharePtr<StreamFrame> (deprecated)");
}

// PoolMakeUnique: RAII wrapper with a stateless deleter. No shared_ptr
// control block, no atomic refcount on the IAlloter. Should be ~equivalent
// to PoolNew + manual PoolDelete while retaining exception safety.
static void BM_PoolEfficiency_StreamFrame_PoolUnique(benchmark::State& state) {
    auto alloter = common::MakePoolAlloterPtr();
    common::AlloterWrap wrap(alloter);

    for (auto _ : state) {
        auto f = wrap.PoolMakeUnique<quic::StreamFrame>();
        f->SetStreamID(4);
        f->SetOffset(0);
        benchmark::DoNotOptimize(f.get());
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("PoolMakeUnique<StreamFrame>");
}

// ===========================================================================
// 12. End-to-end "Connection" scenario (Gap 1)
// ---------------------------------------------------------------------------
// The simulation: one logical connection repeatedly builds N frames
// (mix of StreamFrame and AckFrame), does some memcpy work to dirty the
// cache, then releases them. This approximates a send-pipeline round and is
// the only benchmark here that attempts to measure the *cache-locality*
// benefit of a per-connection pool -- the hot-path microbenches keep
// everything in L1 and therefore under-report that benefit.
// ===========================================================================

namespace {

constexpr int kFramesPerRound = 64;
constexpr size_t kScratchBytes = 1200;  // ~MTU-ish memcpy to pollute cache

// Work model: for each frame we do a memcpy of kScratchBytes bytes into a
// per-round scratch buffer. This ensures the allocator's cache-locality
// advantage has a chance to manifest (vs. tight alloc/free loop, where L1
// is always hot regardless of allocator).
struct RoundScratch {
    std::vector<uint8_t> src;
    std::vector<uint8_t> dst;
    RoundScratch() : src(kScratchBytes, 0x5A), dst(kScratchBytes, 0) {}
};

}  // namespace

static void BM_PoolEfficiency_ConnectionScenario_MakeShared(benchmark::State& state) {
    RoundScratch scratch;

    for (auto _ : state) {
        std::vector<std::shared_ptr<quic::IFrame>> frames;
        frames.reserve(kFramesPerRound);

        for (int i = 0; i < kFramesPerRound; ++i) {
            if ((i & 3) == 0) {
                auto af = std::make_shared<quic::AckFrame>();
                af->SetLargestAck(1000 + i);
                af->SetAckDelay(25);
                af->SetFirstAckRange(3);
                af->AddAckRange(1, 2);
                frames.push_back(af);
            } else {
                auto sf = std::make_shared<quic::StreamFrame>();
                sf->SetStreamID(4 + i);
                sf->SetOffset(static_cast<uint64_t>(i) * 1200);
                frames.push_back(sf);
            }

            // cache-dirtying work
            std::memcpy(scratch.dst.data(), scratch.src.data(), scratch.dst.size());
            benchmark::DoNotOptimize(scratch.dst.data());
        }
        // frames released here
    }
    state.SetItemsProcessed(state.iterations() * kFramesPerRound);
    state.SetLabel("make_shared (global allocator)");
}

static void BM_PoolEfficiency_ConnectionScenario_PerConnPool(benchmark::State& state) {
    auto alloter = common::MakePoolAlloterPtr();
    common::AlloterWrap wrap(alloter);
    RoundScratch scratch;

    for (auto _ : state) {
        std::vector<std::shared_ptr<quic::IFrame>> frames;
        frames.reserve(kFramesPerRound);

        for (int i = 0; i < kFramesPerRound; ++i) {
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
            if ((i & 3) == 0) {
                auto af = wrap.PoolNewSharePtr<quic::AckFrame>();
                af->SetLargestAck(1000 + i);
                af->SetAckDelay(25);
                af->SetFirstAckRange(3);
                af->AddAckRange(1, 2);
                frames.push_back(af);
            } else {
                auto sf = wrap.PoolNewSharePtr<quic::StreamFrame>();
                sf->SetStreamID(4 + i);
                sf->SetOffset(static_cast<uint64_t>(i) * 1200);
                frames.push_back(sf);
            }
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

            std::memcpy(scratch.dst.data(), scratch.src.data(), scratch.dst.size());
            benchmark::DoNotOptimize(scratch.dst.data());
        }
    }
    state.SetItemsProcessed(state.iterations() * kFramesPerRound);
    state.SetLabel("per-connection PoolAlloter (deprecated shared_ptr)");
}

// Same scenario, but using raw PoolNew/PoolDelete to remove shared_ptr
// control-block cost. This is what a "native pool user" would write.
static void BM_PoolEfficiency_ConnectionScenario_PerConnPoolRaw(benchmark::State& state) {
    auto alloter = common::MakePoolAlloterPtr();
    common::AlloterWrap wrap(alloter);
    RoundScratch scratch;

    // Tagged union so we know which dtor to call.
    struct Entry {
        quic::IFrame* ptr;
        bool is_ack;
    };

    for (auto _ : state) {
        std::vector<Entry> frames;
        frames.reserve(kFramesPerRound);

        for (int i = 0; i < kFramesPerRound; ++i) {
            if ((i & 3) == 0) {
                auto* af = wrap.PoolNew<quic::AckFrame>();
                af->SetLargestAck(1000 + i);
                af->SetAckDelay(25);
                af->SetFirstAckRange(3);
                af->AddAckRange(1, 2);
                frames.push_back({af, true});
            } else {
                auto* sf = wrap.PoolNew<quic::StreamFrame>();
                sf->SetStreamID(4 + i);
                sf->SetOffset(static_cast<uint64_t>(i) * 1200);
                frames.push_back({sf, false});
            }

            std::memcpy(scratch.dst.data(), scratch.src.data(), scratch.dst.size());
            benchmark::DoNotOptimize(scratch.dst.data());
        }

        for (auto& e : frames) {
            if (e.is_ack) {
                wrap.PoolDelete(static_cast<quic::AckFrame*>(e.ptr));
            } else {
                wrap.PoolDelete(static_cast<quic::StreamFrame*>(e.ptr));
            }
        }
    }
    state.SetItemsProcessed(state.iterations() * kFramesPerRound);
    state.SetLabel("per-connection PoolAlloter (raw ptr)");
}

// Same scenario, but using PoolMakeUnique (zero-overhead RAII). We keep the
// tagged storage so that the destructor is invoked on the concrete type
// (avoids relying on a virtual dtor and keeps sizeof(T) accurate in the
// stateless deleter). This is what we recommend users write when they want
// exception safety without the shared_ptr cost.
static void BM_PoolEfficiency_ConnectionScenario_PerConnPoolUnique(benchmark::State& state) {
    auto alloter = common::MakePoolAlloterPtr();
    common::AlloterWrap wrap(alloter);
    RoundScratch scratch;

    struct Entry {
        common::PoolUniquePtr<quic::StreamFrame> sf;
        common::PoolUniquePtr<quic::AckFrame>    af;
    };

    for (auto _ : state) {
        std::vector<Entry> frames;
        frames.reserve(kFramesPerRound);

        for (int i = 0; i < kFramesPerRound; ++i) {
            Entry e;
            if ((i & 3) == 0) {
                e.af = wrap.PoolMakeUnique<quic::AckFrame>();
                e.af->SetLargestAck(1000 + i);
                e.af->SetAckDelay(25);
                e.af->SetFirstAckRange(3);
                e.af->AddAckRange(1, 2);
            } else {
                e.sf = wrap.PoolMakeUnique<quic::StreamFrame>();
                e.sf->SetStreamID(4 + i);
                e.sf->SetOffset(static_cast<uint64_t>(i) * 1200);
            }
            frames.push_back(std::move(e));

            std::memcpy(scratch.dst.data(), scratch.src.data(), scratch.dst.size());
            benchmark::DoNotOptimize(scratch.dst.data());
        }
        // frames goes out of scope here -> unique_ptrs call PoolDeleter.
    }
    state.SetItemsProcessed(state.iterations() * kFramesPerRound);
    state.SetLabel("per-connection PoolAlloter (PoolMakeUnique)");
}

// ===========================================================================
// 13. "Many small pools vs one big pool" (Gap 6)
// ---------------------------------------------------------------------------
// "Per-connection pool" amounts to having thousands of PoolAlloter instances
// (one per Connection). This benchmark contrasts that model against a
// single shared pool doing the same total number of ops, to quantify
//   (a) warmup / first-chunk amortisation overhead, and
//   (b) resident-set overhead from unused reserve across many cold pools.
// ===========================================================================

static void BM_PoolEfficiency_ManyPools_Distributed(benchmark::State& state) {
    const int num_pools = static_cast<int>(state.range(0));
    const int ops_per_pool = 128;

    for (auto _ : state) {
        // Create pools, do a burst of ops on each, destroy all.
        // Mirrors "short-lived connection" pattern.
        std::vector<std::shared_ptr<common::IAlloter>> pools;
        pools.reserve(num_pools);
        for (int p = 0; p < num_pools; ++p) {
            pools.push_back(common::MakePoolAlloterPtr());
        }

        for (int p = 0; p < num_pools; ++p) {
            auto& a = pools[p];
            for (int i = 0; i < ops_per_pool; ++i) {
                void* ptr = a->Malloc(64);
                benchmark::DoNotOptimize(ptr);
                a->Free(ptr, 64);
            }
        }
        pools.clear();  // release chunks
    }
    state.SetItemsProcessed(state.iterations() *
                            static_cast<uint64_t>(num_pools) * ops_per_pool);
    state.counters["pools"]         = benchmark::Counter(static_cast<double>(num_pools));
    state.counters["ops_per_pool"]  = benchmark::Counter(static_cast<double>(ops_per_pool));
}

static void BM_PoolEfficiency_ManyPools_SingleBig(benchmark::State& state) {
    const int num_pools = static_cast<int>(state.range(0));  // same denominator
    const int ops_per_pool = 128;
    const int total_ops = num_pools * ops_per_pool;

    for (auto _ : state) {
        auto a = common::MakePoolAlloterPtr();
        for (int i = 0; i < total_ops; ++i) {
            void* ptr = a->Malloc(64);
            benchmark::DoNotOptimize(ptr);
            a->Free(ptr, 64);
        }
    }
    state.SetItemsProcessed(state.iterations() * total_ops);
    state.counters["pools"]         = benchmark::Counter(1.0);
    state.counters["ops_per_pool"]  = benchmark::Counter(static_cast<double>(total_ops));
}

}  // namespace perf
}  // namespace quicx

// ===========================================================================
// Register Benchmarks
// ===========================================================================

// 1. PoolAlloter vs std::malloc
BENCHMARK(quicx::perf::BM_PoolEfficiency_PoolAlloterVsMalloc_Pool)
    ->Arg(16)->Arg(32)->Arg(64)->Arg(128)->Arg(256);
BENCHMARK(quicx::perf::BM_PoolEfficiency_PoolAlloterVsMalloc_Malloc)
    ->Arg(16)->Arg(32)->Arg(64)->Arg(128)->Arg(256);

// 2. BlockMemoryPool vs std::malloc
BENCHMARK(quicx::perf::BM_PoolEfficiency_BlockPoolVsMalloc_Pool)
    ->Arg(1024)->Arg(2048)->Arg(4096)->Arg(16384);
BENCHMARK(quicx::perf::BM_PoolEfficiency_BlockPoolVsMalloc_Malloc)
    ->Arg(1024)->Arg(2048)->Arg(4096)->Arg(16384);

// 3. Mixed workload
BENCHMARK(quicx::perf::BM_PoolEfficiency_MixedWorkload_Pool);
BENCHMARK(quicx::perf::BM_PoolEfficiency_MixedWorkload_Malloc);

// 4. Buffer per-packet
BENCHMARK(quicx::perf::BM_PoolEfficiency_BufferPerPacket)
    ->Arg(10)->Arg(100)->Arg(1000);

// 5. Pool expansion
BENCHMARK(quicx::perf::BM_PoolEfficiency_PoolExpansion)
    ->Arg(10)->Arg(50)->Arg(200)->Arg(500);

// 6. Multi-threaded contention
BENCHMARK(quicx::perf::BM_PoolEfficiency_MultiThreadContention)
    ->Arg(1)->Arg(2)->Arg(4)->Arg(8);

// 7. Real-world size distribution
BENCHMARK(quicx::perf::BM_PoolEfficiency_RealWorldSizeDistribution);

// 8. NormalAlloter baseline
BENCHMARK(quicx::perf::BM_PoolEfficiency_NormalAlloter)
    ->Arg(16)->Arg(64)->Arg(256)->Arg(1024);

// 9. Pool fall-through for sizes > kDefaultMaxBytes (edge case)
BENCHMARK(quicx::perf::BM_PoolEfficiency_PoolFallthrough_Large)
    ->Arg(512)->Arg(1024)->Arg(4096);

// 10. Tail-latency comparison
BENCHMARK(quicx::perf::BM_PoolEfficiency_Latency_Pool)->Iterations(200000);
BENCHMARK(quicx::perf::BM_PoolEfficiency_Latency_Malloc)->Iterations(200000);

// 11. Real-type StreamFrame: make_shared vs Pool variants
BENCHMARK(quicx::perf::BM_PoolEfficiency_StreamFrame_MakeShared);
BENCHMARK(quicx::perf::BM_PoolEfficiency_StreamFrame_PoolRaw);
BENCHMARK(quicx::perf::BM_PoolEfficiency_StreamFrame_PoolUnique);
BENCHMARK(quicx::perf::BM_PoolEfficiency_StreamFrame_PoolSharePtr);

// 12. End-to-end connection scenario (frames + cache-dirtying work)
BENCHMARK(quicx::perf::BM_PoolEfficiency_ConnectionScenario_MakeShared);
BENCHMARK(quicx::perf::BM_PoolEfficiency_ConnectionScenario_PerConnPool);
BENCHMARK(quicx::perf::BM_PoolEfficiency_ConnectionScenario_PerConnPoolRaw);
BENCHMARK(quicx::perf::BM_PoolEfficiency_ConnectionScenario_PerConnPoolUnique);

// 13. Many small pools (per-connection model) vs one big pool
BENCHMARK(quicx::perf::BM_PoolEfficiency_ManyPools_Distributed)
    ->Arg(10)->Arg(100)->Arg(1000)->Arg(10000);
BENCHMARK(quicx::perf::BM_PoolEfficiency_ManyPools_SingleBig)
    ->Arg(10)->Arg(100)->Arg(1000)->Arg(10000);

BENCHMARK_MAIN();

#else
int main() {
    return 0;
}
#endif
