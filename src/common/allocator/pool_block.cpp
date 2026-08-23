#include <atomic>
#include <cstdlib>
#include <thread>

#include <quicx/common/metrics.h>
#include <quicx/common/metrics_std.h>
#include "common/allocator/pool_block.h"
#include "common/log/log.h"

namespace quicx {
namespace common {

static const uint16_t kMaxBlockNum = 20;

BlockMemoryPool::BlockMemoryPool(uint32_t large_sz, uint32_t add_num):
    number_large_add_nodes_(add_num),
    large_size_(large_sz),
    owner_tid_(std::thread::id()) {}

BlockMemoryPool::~BlockMemoryPool() {
    // Free all blocks still in the owner's free list.
    for (auto iter = free_mem_vec_.begin(); iter != free_mem_vec_.end(); ++iter) {
        free(*iter);
    }
    free_mem_vec_.clear();

    // Any block still queued for handback was returned to THIS pool by a foreign
    // thread but never drained (e.g. the pool is destroyed at teardown while a
    // worker thread still held in-flight packets). Those blocks are not
    // referenced by any live object, so free them directly to avoid leaking.
    void* batch = handback_head_.exchange(nullptr, std::memory_order_acquire);
    while (batch) {
        void* next = *reinterpret_cast<void**>(batch);
        free(batch);
        batch = next;
    }
}

void* BlockMemoryPool::PoolLargeMalloc() {
    // Self-heal ownership: if this pool was never given an owner, the first
    // thread that allocates from it becomes the owner. This keeps the
    // ownership metadata stable even for pools whose creator forgot
    // SetOwnerThread().
    std::thread::id expected = std::thread::id();
    owner_tid_.compare_exchange_strong(expected, std::this_thread::get_id(),
                                       std::memory_order_relaxed,
                                       std::memory_order_relaxed);

    // Reclaim foreign-thread frees before we may need to expand. Any thread
    // may malloc from a pool whose buffers it holds (e.g. a worker thread
    // writing into a stream created by the main thread), so free_mem_vec_ is
    // guarded by free_vec_mtx_ everywhere.
    DrainHandback();

    void* ret = nullptr;
    {
        std::lock_guard<std::mutex> lg(free_vec_mtx_);  // TODO remove this lock
        if (free_mem_vec_.empty()) {
            Expansion();
        }
        ret = free_mem_vec_.back();
        free_mem_vec_.pop_back();
    }

    // Metrics: Memory allocated
    common::Metrics::GaugeInc(common::MetricsStd::MemPoolAllocatedBlocks);
    common::Metrics::GaugeDec(common::MetricsStd::MemPoolFreeBlocks);
    common::Metrics::CounterInc(common::MetricsStd::MemPoolAllocations);

    return ret;
}

void BlockMemoryPool::PoolLargeFree(void*& m) {
    // Fast path: ONLY when this thread is definitively the recorded owner.
    // The acquire load pairs with the release store in SetOwnerThread(), so a
    // foreign thread either sees the real owner id (and defers via handback)
    // or the default id (also handback). The old "owner not yet known == take
    // the fast path" rule let foreign threads push onto free_mem_vec_
    // concurrently with the owner — a heap-corrupting data race.
    if (std::this_thread::get_id() == owner_tid_.load(std::memory_order_acquire)) {
        {
            std::lock_guard<std::mutex> lg(free_vec_mtx_);
            free_mem_vec_.push_back(m);
            if (free_mem_vec_.size() > kMaxBlockNum) {
                ReleaseHalf();
            }
        }
        m = nullptr;

        // Metrics: Memory deallocated
        common::Metrics::GaugeDec(common::MetricsStd::MemPoolAllocatedBlocks);
        common::Metrics::GaugeInc(common::MetricsStd::MemPoolFreeBlocks);
        common::Metrics::CounterInc(common::MetricsStd::MemPoolDeallocations);
        return;
    }

    // Cross-thread free. Push the block onto the lock-free handback stack. We
    // reuse the freed block's own first 8 bytes as the intrusive next pointer,
    // so this is allocation-free and needs no event loop. The owner thread
    // reclaims it via DrainHandback() (called from PoolLargeMalloc / ~dtor).
    //
    // Intrusive link: the block is not in free_mem_vec_ while queued here, so
    // overwriting its first 8 bytes is safe. malloc() guarantees >= 8-byte
    // alignment on 64-bit platforms.
    void* head = handback_head_.load(std::memory_order_relaxed);
    do {
        *reinterpret_cast<void**>(m) = head;
    } while (!handback_head_.compare_exchange_weak(
        head, m, std::memory_order_release, std::memory_order_relaxed));
    m = nullptr;

    // Metrics: Memory deallocated (gauges are atomic, safe from any thread).
    common::Metrics::GaugeDec(common::MetricsStd::MemPoolAllocatedBlocks);
    common::Metrics::GaugeInc(common::MetricsStd::MemPoolFreeBlocks);
    common::Metrics::CounterInc(common::MetricsStd::MemPoolDeallocations);
}

uint32_t BlockMemoryPool::GetSize() {
    std::lock_guard<std::mutex> lg(free_vec_mtx_);
    return (uint32_t)free_mem_vec_.size();
}

uint32_t BlockMemoryPool::GetBlockLength() {
    return large_size_;
}

void BlockMemoryPool::SetOwnerThread(std::thread::id owner) {
    // Record the first owner only; a later call from a different thread must
    // never steal ownership of a pool another thread is already using.
    std::thread::id expected = std::thread::id();
    owner_tid_.compare_exchange_strong(expected, owner,
                                       std::memory_order_release,
                                       std::memory_order_relaxed);
}

void BlockMemoryPool::DrainHandback() {
    void* batch = handback_head_.exchange(nullptr, std::memory_order_acquire);
    if (!batch) {
        return;
    }
    // Splice the reclaimed blocks into the free list, then trim if needed.
    // free_vec_mtx_ also covers ReleaseHalf()/Expansion() called below.
    {
        std::lock_guard<std::mutex> lg(free_vec_mtx_);
        while (batch) {
            void* next = *reinterpret_cast<void**>(batch);
            free_mem_vec_.push_back(batch);
            batch = next;
        }
        // If the drain pushed a large batch back, trim the free list.
        if (free_mem_vec_.size() > kMaxBlockNum) {
            ReleaseHalf();
        }
    }
}

void BlockMemoryPool::ReleaseHalf() {
    // Caller must hold free_vec_mtx_ (any thread may malloc/free now).
    size_t half = free_mem_vec_.size() / 2;

    // Free first half of the vector
    for (size_t i = 0; i < half; i++) {
        free(free_mem_vec_[i]);
    }

    // Remove freed pointers from vector
    free_mem_vec_.erase(free_mem_vec_.begin(), free_mem_vec_.begin() + half);
}

void BlockMemoryPool::Expansion(uint32_t num) {
    if (num == 0) {
        num = number_large_add_nodes_;
    }

    for (uint32_t i = 0; i < num; ++i) {
        void* mem = malloc(large_size_);
        if (mem == nullptr) {
            LOG_ERROR("BlockMemoryPool::Expansion: malloc(%u) failed", large_size_);
            break;
        }
        // not memset!
        free_mem_vec_.push_back(mem);
    }

    // Metrics: Pool expanded
    common::Metrics::GaugeInc(common::MetricsStd::MemPoolFreeBlocks, num);
}

std::shared_ptr<common::BlockMemoryPool> MakeBlockMemoryPoolPtr(uint32_t large_sz, uint32_t add_num) {
    return std::make_shared<BlockMemoryPool>(large_sz, add_num);
}

}  // namespace common
}  // namespace quicx