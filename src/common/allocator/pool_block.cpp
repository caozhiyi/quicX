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
    // Reclaim foreign-thread frees before we may need to expand.
    DrainHandback();

    if (free_mem_vec_.empty()) {
        Expansion();
    }

    void* ret = free_mem_vec_.back();
    free_mem_vec_.pop_back();

    // Metrics: Memory allocated
    common::Metrics::GaugeInc(common::MetricsStd::MemPoolAllocatedBlocks);
    common::Metrics::GaugeDec(common::MetricsStd::MemPoolFreeBlocks);
    common::Metrics::CounterInc(common::MetricsStd::MemPoolAllocations);

    return ret;
}

void BlockMemoryPool::PoolLargeFree(void*& m) {
    // Fast path: same-thread free (the common case), or owner not yet known.
    // The free list is only ever touched by the owner thread, so no atomic ops
    // or allocation are needed here — exactly as in the original design.
    if (owner_tid_ == std::thread::id() || std::this_thread::get_id() == owner_tid_) {
        free_mem_vec_.push_back(m);
        m = nullptr;

        // Metrics: Memory deallocated
        common::Metrics::GaugeDec(common::MetricsStd::MemPoolAllocatedBlocks);
        common::Metrics::GaugeInc(common::MetricsStd::MemPoolFreeBlocks);
        common::Metrics::CounterInc(common::MetricsStd::MemPoolDeallocations);

        if (free_mem_vec_.size() > kMaxBlockNum) {
            ReleaseHalf();
        }
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
    return (uint32_t)free_mem_vec_.size();
}

uint32_t BlockMemoryPool::GetBlockLength() {
    return large_size_;
}

void BlockMemoryPool::SetOwnerThread(std::thread::id owner) {
    owner_tid_ = owner;
}

void BlockMemoryPool::DrainHandback() {
    void* batch = handback_head_.exchange(nullptr, std::memory_order_acquire);
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

void BlockMemoryPool::ReleaseHalf() {
    // No lock needed - always called from owning thread via RunInLoop
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