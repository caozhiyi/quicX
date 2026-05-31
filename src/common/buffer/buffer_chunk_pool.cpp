#include <vector>
#include <cstdio>   // PERF VALIDATION: stderr stats
#include <cstdlib>  // PERF VALIDATION: getenv
#include <cstring>  // PERF VALIDATION: strcmp
#include <atomic>

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_chunk.h"
#include "common/buffer/buffer_chunk_pool.h"

namespace quicx {
namespace common {

namespace {

// PERF VALIDATION: A/B switch. Set QUICX_BUFFERCHUNKPOOL=0 to bypass the
// recycler and fall back to the legacy std::make_shared<BufferChunk>() path,
// so we can A/B benchmark the pool's contribution within the same binary
// without git-stash gymnastics. Any other value (or unset) keeps recycling on.
bool PoolEnabled() {
    static const bool enabled = []() {
        const char* v = std::getenv("QUICX_BUFFERCHUNKPOOL");
        if (v == nullptr) return true;
        // "0" / "off" / "false" disables; everything else enables.
        if (std::strcmp(v, "0") == 0) return false;
        if (std::strcmp(v, "off") == 0) return false;
        if (std::strcmp(v, "false") == 0) return false;
        return true;
    }();
    return enabled;
}

// Tunable: hard cap to avoid unbounded growth if a producer outpaces
// consumption. Each cached entry holds one BlockMemoryPool block (1500B by
// default) plus the BufferChunk object itself.
constexpr std::size_t kFreeListCap = 256;

// PERF VALIDATION: global atomic counters across all worker threads. We
// cannot rely on thread_local destructors firing under std::exit() because
// only the *current* thread's thread_locals get destroyed; QUIC worker
// threads typically aren't the thread that calls exit(). Counting globally
// (and dumping in a global dtor) guarantees the reports survive shutdown.
std::atomic<uint64_t> g_acquires{0};
std::atomic<uint64_t> g_recycled{0};
std::atomic<uint64_t> g_fresh{0};
std::atomic<uint64_t> g_drops_full{0};   // Recycle path: free list full → delete
std::atomic<uint64_t> g_drops_invalid{0}; // Recycle path: chunk invalid → delete
std::atomic<uint64_t> g_high_water{0};    // peak free-list size observed (per thread, max-merged)

struct GlobalReporter {
    ~GlobalReporter() {
        std::fprintf(stderr,
                     "[perf] BufferChunkPool global summary: "
                     "acquires=%llu recycled=%llu fresh=%llu "
                     "drops_full=%llu drops_invalid=%llu high_water=%llu\n",
                     static_cast<unsigned long long>(g_acquires.load()),
                     static_cast<unsigned long long>(g_recycled.load()),
                     static_cast<unsigned long long>(g_fresh.load()),
                     static_cast<unsigned long long>(g_drops_full.load()),
                     static_cast<unsigned long long>(g_drops_invalid.load()),
                     static_cast<unsigned long long>(g_high_water.load()));
    }
};
GlobalReporter g_global_reporter;

// Per-thread free list of recycled BufferChunk objects. Each entry still owns
// its pool-managed block (BufferChunk's dtor would normally return it to the
// BlockMemoryPool). When the thread exits, the destructor of this vector
// runs and deletes every cached BufferChunk, which routes blocks back to
// BlockMemoryPool through BufferChunk::~BufferChunk.
struct ThreadLocalFreeList {
    std::vector<BufferChunk*> chunks;

    ~ThreadLocalFreeList() {
        for (auto* c : chunks) {
            delete c;
        }
        chunks.clear();
    }
};

// Defined as a function-local static instead of a translation-unit-level
// thread_local to defer construction until first use AND to keep the symbol
// hidden inside this TU.
ThreadLocalFreeList& Tls() {
    thread_local ThreadLocalFreeList tls;
    return tls;
}

void Recycle(BufferChunk* raw) {
    if (raw == nullptr) {
        return;
    }
    // If the chunk is no longer valid (e.g. its pool was destroyed and
    // BufferChunk::Release() has freed the block), there is nothing to recycle:
    // the wrapper has lost its block and must be deleted normally.
    if (!raw->Valid()) {
        g_drops_invalid.fetch_add(1, std::memory_order_relaxed);
        delete raw;
        return;
    }
    auto& tls = Tls();
    if (tls.chunks.size() >= kFreeListCap) {
        // Cache is full; just delete (which returns the block to BlockMemoryPool
        // via BufferChunk::Release()).
        g_drops_full.fetch_add(1, std::memory_order_relaxed);
        delete raw;
        return;
    }
    // Note: by the time the deleter fires, every SharedBufferSpan that
    // referenced this chunk has already been destroyed, so freeze_count_ is
    // guaranteed to be 0 and write_floor_offset_ is guaranteed to be 0
    // (auto-cleared by the last Unfreeze()). No manual reset is required.
    tls.chunks.push_back(raw);

    // High-water mark (best effort, not perfectly atomic across threads).
    uint64_t cur = static_cast<uint64_t>(tls.chunks.size());
    uint64_t prev = g_high_water.load(std::memory_order_relaxed);
    while (cur > prev &&
           !g_high_water.compare_exchange_weak(prev, cur,
                                               std::memory_order_relaxed)) {
        // retry
    }
}

}  // namespace

std::shared_ptr<BufferChunk> BufferChunkPool::Acquire(
        const std::shared_ptr<BlockMemoryPool>& pool) {
    if (!pool) {
        return nullptr;
    }
    g_acquires.fetch_add(1, std::memory_order_relaxed);

    // PERF VALIDATION: bypass path for A/B benchmarking. Mirrors legacy
    // behavior: each call performs std::make_shared<BufferChunk>(pool), which
    // means one BufferChunk ctor + one combined object/control-block alloc,
    // and the default deleter on dtor (chunk returns its block to
    // BlockMemoryPool, then frees the wrapper). No recycling.
    if (!PoolEnabled()) {
        g_fresh.fetch_add(1, std::memory_order_relaxed);
        auto chunk = std::make_shared<BufferChunk>(pool);
        if (!chunk->Valid()) {
            return nullptr;
        }
        return chunk;
    }

    auto& tls = Tls();
    BufferChunk* raw = nullptr;
    if (!tls.chunks.empty()) {
        raw = tls.chunks.back();
        tls.chunks.pop_back();
        g_recycled.fetch_add(1, std::memory_order_relaxed);
    } else {
        raw = new BufferChunk(pool);
        if (!raw->Valid()) {
            // ctor logged the failure already; surface as nullptr so callers
            // can short-circuit just like they did with the legacy
            // std::make_shared<BufferChunk> path (which would also yield an
            // invalid chunk).
            delete raw;
            return nullptr;
        }
        g_fresh.fetch_add(1, std::memory_order_relaxed);
    }
    // Wrap with a custom deleter that returns the raw to the free list.
    // shared_ptr's control block is the only remaining per-call heap
    // allocation in this path; it is unavoidable without a more invasive
    // intrusive_ptr refactor.
    return std::shared_ptr<BufferChunk>(raw, &Recycle);
}

}  // namespace common
}  // namespace quicx
