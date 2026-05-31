#include <cstdlib>

#include "common/alloter/pool_block.h"
#include "common/log/log.h"
#include <quicx/common/metrics.h>
#include <quicx/common/metrics_std.h>
#include <quicx/common/if_event_loop.h>

namespace quicx {
namespace common {

static const uint16_t kMaxBlockNum = 20;

BlockMemoryPool::BlockMemoryPool(uint32_t large_sz, uint32_t add_num):
    number_large_add_nodes_(add_num),
    large_size_(large_sz) {}

BlockMemoryPool::~BlockMemoryPool() {
    // free all memory
    for (auto iter = free_mem_vec_.begin(); iter != free_mem_vec_.end(); ++iter) {
        free(*iter);
    }
    free_mem_vec_.clear();
}

void* BlockMemoryPool::PoolLargeMalloc() {
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
    // Fast path: same-thread free (the common case).
    //
    // BlockMemoryPool is held in a thread_local on each QUIC worker, and so
    // is the worker's IEventLoop. BufferChunk objects are allocated and
    // released by the same worker that owns the pool, so the vast majority
    // of PoolLargeFree calls happen on the owning thread. In that case we
    // skip the RunInLoop wrapper entirely - that wrapper allocates a
    // std::function (potentially heap), copies a weak_ptr (atomic op), and
    // re-acquires the shared_ptr inside the lambda (another atomic). All
    // pure overhead when there is no thread switch.
    //
    // Cross-thread fallback (kept for safety): if some unusual teardown
    // path drops the last BufferChunk reference on a different thread, we
    // still serialise the free_mem_vec_ mutation back onto the owning
    // thread via PostTask so the std::vector stays single-threaded.
    auto loop = event_loop_.lock();
    if (loop && !loop->IsInLoopThread()) {
        void* ptr = m;
        auto weak_self = weak_from_this();
        loop->RunInLoop([weak_self, ptr]() mutable {
            auto self = weak_self.lock();
            if (!self) {
                // Pool already destroyed, free the raw memory directly
                free(ptr);
                return;
            }
            self->free_mem_vec_.push_back(ptr);

            // Metrics: Memory deallocated
            common::Metrics::GaugeDec(common::MetricsStd::MemPoolAllocatedBlocks);
            common::Metrics::GaugeInc(common::MetricsStd::MemPoolFreeBlocks);
            common::Metrics::CounterInc(common::MetricsStd::MemPoolDeallocations);

            if (self->free_mem_vec_.size() > kMaxBlockNum) {
                // Safe to call ReleaseHalf - we're in the owning thread
                self->ReleaseHalf();
            }
        });
        m = nullptr;
        return;
    }

    // Same-thread (or no event loop, e.g. tests / shutdown): mutate the
    // free list directly. No lambda construction, no atomic ref-count ops.
    free_mem_vec_.push_back(m);
    m = nullptr;

    // Metrics: Memory deallocated
    common::Metrics::GaugeDec(common::MetricsStd::MemPoolAllocatedBlocks);
    common::Metrics::GaugeInc(common::MetricsStd::MemPoolFreeBlocks);
    common::Metrics::CounterInc(common::MetricsStd::MemPoolDeallocations);

    if (free_mem_vec_.size() > kMaxBlockNum) {
        ReleaseHalf();
    }
}

uint32_t BlockMemoryPool::GetSize() {
    return (uint32_t)free_mem_vec_.size();
}

uint32_t BlockMemoryPool::GetBlockLength() {
    return large_size_;
}

void BlockMemoryPool::SetEventLoop(std::shared_ptr<IEventLoop> loop) {
    event_loop_ = loop;
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