// =============================================================================
// memory_baseline_test.cpp - Memory Usage Baseline Benchmarks for quicX
// =============================================================================
//
// Establishes memory usage baselines for key data structures.
// Measures per-connection, per-stream, and per-buffer memory footprint.
//
// Build:
//   cmake -B build -DENABLE_PERF_TESTS=ON && cmake --build build -j
//
// Usage:
//   ./build/bin/perf/memory_baseline_test
//   ./build/bin/perf/memory_baseline_test --benchmark_filter="PerConnection"
//
// =============================================================================

#if defined(QUICX_ENABLE_BENCHMARKS)

#include <benchmark/benchmark.h>
#include <cstring>
#include <memory>
#include <vector>
#include <string>
#include <map>
#include <atomic>
#include <thread>
#include <chrono>
#include <numeric>
#include <algorithm>

// Platform-specific includes for memory tracking (must be at top level)
#if defined(__APPLE__)
#include <malloc/malloc.h>
#include <mach/mach.h>
#include <unistd.h>
#elif defined(__linux__)
#include <malloc.h>
#include <unistd.h>
#endif

// Common
#include "common/alloter/pool_alloter.h"
#include "common/alloter/pool_block.h"
#include "common/buffer/if_buffer.h"
#include "common/buffer/multi_block_buffer.h"

namespace quicx {
namespace perf {

// ===========================================================================
// Memory tracking utilities
// ===========================================================================

// Simple memory tracking using malloc_size (macOS) / malloc_usable_size (Linux)
static size_t GetAllocSize(void* ptr) {
#if defined(__APPLE__)
    return ptr ? malloc_size(ptr) : 0;
#elif defined(__linux__)
    return ptr ? malloc_usable_size(ptr) : 0;
#else
    (void)ptr;
    return 0;
#endif
}

// Track current process RSS (resident set size) on macOS/Linux
static size_t GetCurrentRSS() {
#if defined(__APPLE__)
    struct task_basic_info info;
    mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&info, &count) == KERN_SUCCESS) {
        return info.resident_size;
    }
    return 0;
#elif defined(__linux__)
    FILE* f = fopen("/proc/self/statm", "r");
    if (!f) return 0;
    long pages = 0;
    if (fscanf(f, "%*ld %ld", &pages) != 1) pages = 0;
    fclose(f);
    return pages * sysconf(_SC_PAGESIZE);
#else
    return 0;
#endif
}

// ===========================================================================
// Benchmark 1: Per-Buffer Memory Footprint
// ===========================================================================

static void BM_MemoryBaseline_BufferFootprint(benchmark::State& state) {
    const size_t buffer_capacity = static_cast<size_t>(state.range(0));

    for (auto _ : state) {
        size_t rss_before = GetCurrentRSS();

        auto pool = std::make_shared<common::BlockMemoryPool>(buffer_capacity, 128);
        auto buf = std::make_shared<common::MultiBlockBuffer>(pool);

        // Write some data to allocate internal memory
        std::vector<uint8_t> data(buffer_capacity / 2, 0xAB);
        buf->Write(data.data(), static_cast<uint32_t>(data.size()));

        size_t rss_after = GetCurrentRSS();

        benchmark::DoNotOptimize(buf);
        state.counters["rss_delta_bytes"] = benchmark::Counter(
            static_cast<double>(rss_after > rss_before ? rss_after - rss_before : 0));
    }
}

// ===========================================================================
// Benchmark 2: Pool Allocator Memory Overhead
// ===========================================================================

static void BM_MemoryBaseline_PoolAllocatorOverhead(benchmark::State& state) {
    const int num_allocs = static_cast<int>(state.range(0));
    const uint32_t alloc_size = 64;  // typical small object

    for (auto _ : state) {
        auto alloc = common::MakePoolAlloterPtr();
        std::vector<void*> ptrs;
        ptrs.reserve(num_allocs);

        size_t rss_before = GetCurrentRSS();

        for (int i = 0; i < num_allocs; ++i) {
            void* ptr = alloc->Malloc(alloc_size);
            ptrs.push_back(ptr);
        }

        size_t rss_after = GetCurrentRSS();

        // Ideal memory = num_allocs * alloc_size
        size_t ideal = num_allocs * alloc_size;
        size_t actual = rss_after > rss_before ? rss_after - rss_before : 0;

        state.counters["ideal_bytes"] = benchmark::Counter(static_cast<double>(ideal));
        state.counters["actual_bytes"] = benchmark::Counter(static_cast<double>(actual));
        if (ideal > 0 && actual > 0) {
            state.counters["overhead_ratio"] = benchmark::Counter(
                static_cast<double>(actual) / static_cast<double>(ideal));
        }

        // Cleanup
        for (auto ptr : ptrs) {
            alloc->Free(ptr, alloc_size);
        }
    }
}

// ===========================================================================
// Benchmark 3: Block Memory Pool Efficiency
// ===========================================================================

static void BM_MemoryBaseline_BlockPoolEfficiency(benchmark::State& state) {
    const uint32_t block_size = static_cast<uint32_t>(state.range(0));
    const int num_blocks = 100;

    for (auto _ : state) {
        auto pool = common::MakeBlockMemoryPoolPtr(block_size, 32);
        std::vector<void*> blocks;
        blocks.reserve(num_blocks);

        for (int i = 0; i < num_blocks; ++i) {
            void* ptr = pool->PoolLargeMalloc();
            blocks.push_back(ptr);
        }

        state.counters["pool_size"] = benchmark::Counter(
            static_cast<double>(pool->GetSize()));
        state.counters["block_length"] = benchmark::Counter(
            static_cast<double>(pool->GetBlockLength()));
        state.counters["total_requested"] = benchmark::Counter(
            static_cast<double>(num_blocks * block_size));

        // Free all
        for (auto ptr : blocks) {
            pool->PoolLargeFree(ptr);
        }

        // After free, pool still holds memory
        state.counters["pool_size_after_free"] = benchmark::Counter(
            static_cast<double>(pool->GetSize()));
    }
}

// ===========================================================================
// Benchmark 4: Many Small Buffers (simulates per-stream buffers)
// ===========================================================================

static void BM_MemoryBaseline_ManySmallBuffers(benchmark::State& state) {
    const int num_buffers = static_cast<int>(state.range(0));
    const size_t buf_size = 4096;  // typical stream buffer

    for (auto _ : state) {
        size_t rss_before = GetCurrentRSS();

        std::vector<std::shared_ptr<common::IBuffer>> buffers;
        buffers.reserve(num_buffers);

        for (int i = 0; i < num_buffers; ++i) {
            auto pool = std::make_shared<common::BlockMemoryPool>(buf_size, 4);
            auto buf = std::make_shared<common::MultiBlockBuffer>(pool);
            // Write a small amount of data (header-like)
            uint8_t data[128] = {};
            buf->Write(data, sizeof(data));
            buffers.push_back(buf);
        }

        size_t rss_after = GetCurrentRSS();

        state.counters["total_buffers"] = benchmark::Counter(
            static_cast<double>(num_buffers));
        state.counters["rss_per_buffer"] = benchmark::Counter(
            num_buffers > 0 && rss_after > rss_before
                ? static_cast<double>(rss_after - rss_before) / num_buffers
                : 0);

        benchmark::DoNotOptimize(buffers.data());
    }
}

// ===========================================================================
// Benchmark 5: Long-running Memory Stability
// ===========================================================================
// Simulates allocation/deallocation cycles to detect memory growth.

static void BM_MemoryBaseline_AllocFreeStability(benchmark::State& state) {
    const int iterations = 10000;
    auto alloc = common::MakePoolAlloterPtr();

    size_t initial_rss = GetCurrentRSS();

    for (auto _ : state) {
        // Simulate allocation/free cycles
        for (int i = 0; i < iterations; ++i) {
            // Allocate varying sizes (simulates different frame types)
            uint32_t sizes[] = {16, 32, 64, 128, 256};
            void* ptrs[5];

            for (int j = 0; j < 5; ++j) {
                ptrs[j] = alloc->Malloc(sizes[j]);
            }

            // Free in reverse order (common pattern)
            for (int j = 4; j >= 0; --j) {
                alloc->Free(ptrs[j], sizes[j]);
            }
        }
    }

    size_t final_rss = GetCurrentRSS();

    state.counters["initial_rss_kb"] = benchmark::Counter(
        static_cast<double>(initial_rss) / 1024.0);
    state.counters["final_rss_kb"] = benchmark::Counter(
        static_cast<double>(final_rss) / 1024.0);
    state.counters["rss_growth_kb"] = benchmark::Counter(
        static_cast<double>(final_rss > initial_rss ? final_rss - initial_rss : 0) / 1024.0);
}

// ===========================================================================
// Benchmark 6: Buffer Chain Memory Usage
// ===========================================================================

static void BM_MemoryBaseline_BufferChainGrowth(benchmark::State& state) {
    const size_t total_write = static_cast<size_t>(state.range(0));
    const size_t chunk_write = 256;  // Write in small chunks

    for (auto _ : state) {
        auto pool = std::make_shared<common::BlockMemoryPool>(4096, 32);
        auto buf = std::make_shared<common::MultiBlockBuffer>(pool);

        std::vector<uint8_t> data(chunk_write, 0xCD);
        size_t written = 0;

        while (written < total_write) {
            size_t to_write = std::min(chunk_write, total_write - written);
            buf->Write(data.data(), static_cast<uint32_t>(to_write));
            written += to_write;
        }

        state.counters["data_length"] = benchmark::Counter(
            static_cast<double>(buf->GetDataLength()));
        state.counters["chunk_count"] = benchmark::Counter(
            static_cast<double>(buf->GetChunkCount()));

        benchmark::DoNotOptimize(buf);
    }
    state.SetBytesProcessed(state.iterations() * total_write);
}

// ===========================================================================
// Benchmark 7: Shared Pointer Overhead (common in quicX)
// ===========================================================================

static void BM_MemoryBaseline_SharedPtrOverhead(benchmark::State& state) {
    const int count = static_cast<int>(state.range(0));

    for (auto _ : state) {
        std::vector<std::shared_ptr<common::BlockMemoryPool>> pools;
        pools.reserve(count);

        for (int i = 0; i < count; ++i) {
            pools.push_back(std::make_shared<common::BlockMemoryPool>(1024, 4));
        }

        benchmark::DoNotOptimize(pools.data());
    }
}

// ===========================================================================
// Benchmark 8: ReleaseHalf Memory Recovery
// ===========================================================================

static void BM_MemoryBaseline_PoolReleaseHalf(benchmark::State& state) {
    const uint32_t block_size = static_cast<uint32_t>(state.range(0));
    const int num_allocs = 200;

    for (auto _ : state) {
        auto pool = common::MakeBlockMemoryPoolPtr(block_size, 64);

        // Allocate many blocks
        std::vector<void*> ptrs;
        ptrs.reserve(num_allocs);
        for (int i = 0; i < num_allocs; ++i) {
            ptrs.push_back(pool->PoolLargeMalloc());
        }

        // Free all (returned to pool, not OS)
        for (auto& ptr : ptrs) {
            pool->PoolLargeFree(ptr);
        }

        uint32_t size_before = pool->GetSize();

        // ReleaseHalf to return memory
        pool->ReleaseHalf();

        uint32_t size_after = pool->GetSize();

        state.counters["pool_before"] = benchmark::Counter(
            static_cast<double>(size_before));
        state.counters["pool_after"] = benchmark::Counter(
            static_cast<double>(size_after));
        state.counters["released"] = benchmark::Counter(
            static_cast<double>(size_before - size_after));
    }
}

}  // namespace perf
}  // namespace quicx

// ===========================================================================
// Register Benchmarks
// ===========================================================================

// Buffer footprint
BENCHMARK(quicx::perf::BM_MemoryBaseline_BufferFootprint)
    ->Arg(1024)->Arg(4096)->Arg(16384)->Arg(65536)
    ->Unit(benchmark::kMicrosecond);

// Pool allocator overhead
BENCHMARK(quicx::perf::BM_MemoryBaseline_PoolAllocatorOverhead)
    ->Arg(100)->Arg(1000)->Arg(10000);

// Block pool efficiency
BENCHMARK(quicx::perf::BM_MemoryBaseline_BlockPoolEfficiency)
    ->Arg(1024)->Arg(2048)->Arg(4096)->Arg(16384);

// Many small buffers (per-stream simulation)
BENCHMARK(quicx::perf::BM_MemoryBaseline_ManySmallBuffers)
    ->Arg(10)->Arg(100)->Arg(1000)
    ->Unit(benchmark::kMicrosecond);

// Long-running stability
BENCHMARK(quicx::perf::BM_MemoryBaseline_AllocFreeStability);

// Buffer chain growth
BENCHMARK(quicx::perf::BM_MemoryBaseline_BufferChainGrowth)
    ->Arg(4096)->Arg(16384)->Arg(65536)->Arg(262144)
    ->Unit(benchmark::kMicrosecond);

// Shared pointer overhead
BENCHMARK(quicx::perf::BM_MemoryBaseline_SharedPtrOverhead)
    ->Arg(10)->Arg(100)->Arg(1000);

// Pool release half
BENCHMARK(quicx::perf::BM_MemoryBaseline_PoolReleaseHalf)
    ->Arg(1024)->Arg(4096)->Arg(16384);

BENCHMARK_MAIN();

#else
int main() {
    return 0;
}
#endif
