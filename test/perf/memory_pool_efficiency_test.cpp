// =============================================================================
// memory_pool_efficiency_test.cpp - Memory Pool Efficiency Analysis for quicX
// =============================================================================
//
// Analyzes the efficiency of quicX's custom memory pool allocators:
//   - PoolAlloter: Small object pool (slab allocator, ≤256 bytes)
//   - BlockMemoryPool: Large block pool (configurable block size)
//
// Reports:
//   - Allocation throughput vs. std::malloc
//   - Fragmentation ratio
//   - Memory utilization after mixed workloads
//   - Cross-thread contention overhead
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
#include <cstring>
#include <memory>
#include <vector>
#include <random>
#include <thread>
#include <atomic>
#include <algorithm>
#include <numeric>

#include "common/alloter/pool_alloter.h"
#include "common/alloter/pool_block.h"
#include "common/alloter/normal_alloter.h"
#include "common/buffer/if_buffer.h"
#include "common/buffer/multi_block_buffer.h"

namespace quicx {
namespace perf {

// ===========================================================================
// Helpers
// ===========================================================================

static std::mt19937& GetRNG() {
    static std::mt19937 gen(42);
    return gen;
}

// ===========================================================================
// 1. PoolAlloter vs std::malloc throughput
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
// 2. BlockMemoryPool vs std::malloc for large blocks
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
// ===========================================================================

static void BM_PoolEfficiency_MixedWorkload_Pool(benchmark::State& state) {
    auto alloc = common::MakePoolAlloterPtr();
    std::uniform_int_distribution<uint32_t> size_dist(8, 256);

    for (auto _ : state) {
        auto& rng = GetRNG();
        // Simulate realistic allocation pattern:
        // Allocate 10 objects, free 7, allocate 5 more, free all
        std::vector<std::pair<void*, uint32_t>> ptrs;

        // Phase 1: Allocate 10
        for (int i = 0; i < 10; ++i) {
            uint32_t sz = size_dist(rng);
            void* ptr = alloc->Malloc(sz);
            ptrs.push_back({ptr, sz});
        }

        // Phase 2: Free 7 (random selection)
        for (int i = 0; i < 7 && !ptrs.empty(); ++i) {
            size_t idx = rng() % ptrs.size();
            alloc->Free(ptrs[idx].first, ptrs[idx].second);
            ptrs.erase(ptrs.begin() + idx);
        }

        // Phase 3: Allocate 5 more
        for (int i = 0; i < 5; ++i) {
            uint32_t sz = size_dist(rng);
            void* ptr = alloc->Malloc(sz);
            ptrs.push_back({ptr, sz});
        }

        // Phase 4: Free all remaining
        for (auto& [ptr, sz] : ptrs) {
            alloc->Free(ptr, sz);
        }
    }
    state.SetItemsProcessed(state.iterations() * 22);  // 10 + 7 + 5 alloc/free ops
}

static void BM_PoolEfficiency_MixedWorkload_Malloc(benchmark::State& state) {
    std::uniform_int_distribution<size_t> size_dist(8, 256);

    for (auto _ : state) {
        auto& rng = GetRNG();
        std::vector<std::pair<void*, size_t>> ptrs;

        for (int i = 0; i < 10; ++i) {
            size_t sz = size_dist(rng);
            void* ptr = malloc(sz);
            ptrs.push_back({ptr, sz});
        }

        for (int i = 0; i < 7 && !ptrs.empty(); ++i) {
            size_t idx = rng() % ptrs.size();
            free(ptrs[idx].first);
            ptrs.erase(ptrs.begin() + idx);
        }

        for (int i = 0; i < 5; ++i) {
            size_t sz = size_dist(rng);
            void* ptr = malloc(sz);
            ptrs.push_back({ptr, sz});
        }

        for (auto& [ptr, sz] : ptrs) {
            free(ptr);
        }
    }
    state.SetItemsProcessed(state.iterations() * 22);
}

// ===========================================================================
// 4. Buffer allocation pattern (per-packet simulation)
// ===========================================================================

static void BM_PoolEfficiency_BufferPerPacket(benchmark::State& state) {
    const int packets = static_cast<int>(state.range(0));
    auto pool = std::make_shared<common::BlockMemoryPool>(4096, 64);

    for (auto _ : state) {
        for (int i = 0; i < packets; ++i) {
            auto buf = std::make_shared<common::MultiBlockBuffer>(pool);

            // Simulate packet processing: write 1200 bytes, read header, read payload
            uint8_t data[1200] = {};
            buf->Write(data, sizeof(data));

            uint8_t header[32];
            buf->Read(header, sizeof(header));

            uint8_t payload[1168];
            buf->Read(payload, sizeof(payload));

            benchmark::DoNotOptimize(header);
            benchmark::DoNotOptimize(payload);
        }
    }
    state.SetItemsProcessed(state.iterations() * packets);
}

// ===========================================================================
// 5. Pool Expansion behavior
// ===========================================================================

static void BM_PoolEfficiency_PoolExpansion(benchmark::State& state) {
    const int num_blocks = static_cast<int>(state.range(0));
    const uint32_t block_size = 4096;

    for (auto _ : state) {
        // Start with small pool, force multiple expansions
        auto pool = common::MakeBlockMemoryPoolPtr(block_size, 4);

        std::vector<void*> ptrs;
        ptrs.reserve(num_blocks);

        for (int i = 0; i < num_blocks; ++i) {
            void* ptr = pool->PoolLargeMalloc();
            ptrs.push_back(ptr);
        }

        state.counters["final_pool_size"] = benchmark::Counter(
            static_cast<double>(pool->GetSize()));

        // Free all
        for (auto& ptr : ptrs) {
            pool->PoolLargeFree(ptr);
        }

        state.counters["after_free_pool_size"] = benchmark::Counter(
            static_cast<double>(pool->GetSize()));
    }
}

// ===========================================================================
// 6. Multi-threaded pool contention
// ===========================================================================

static void BM_PoolEfficiency_MultiThreadContention(benchmark::State& state) {
    const int num_threads = static_cast<int>(state.range(0));
    const int ops_per_thread = 10000;
    auto pool = common::MakeBlockMemoryPoolPtr(4096, 256);

    for (auto _ : state) {
        std::atomic<uint64_t> total_ops{0};
        std::vector<std::thread> threads;

        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&pool, &total_ops, ops_per_thread]() {
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
    std::vector<uint32_t> typical_sizes = {
        8, 16, 24, 32, 48, 64, 96, 128, 192, 256,  // within pool range
        8, 16, 24, 32, 16, 24, 32, 48, 64, 128,     // biased toward small
    };

    for (auto _ : state) {
        std::vector<std::pair<void*, uint32_t>> ptrs;
        ptrs.reserve(typical_sizes.size());

        // Allocate all
        for (uint32_t sz : typical_sizes) {
            void* ptr = alloc->Malloc(sz);
            ptrs.push_back({ptr, sz});
        }

        // Free all
        for (auto& [ptr, sz] : ptrs) {
            alloc->Free(ptr, sz);
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

BENCHMARK_MAIN();

#else
int main() {
    return 0;
}
#endif
