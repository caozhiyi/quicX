// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#include <cstdlib>

#include "common/alloter/pool_block.h"
#include "common/metrics/metrics.h"
#include "common/metrics/metrics_std.h"
#include "common/network/if_event_loop.h"

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
    auto loop = event_loop_.lock();
    if (loop) {
        // Use RunInLoop to defer free operation to owning thread (lock-free!)
        void* ptr = m;  // Capture by value
        loop->RunInLoop([this, ptr]() mutable {
            free_mem_vec_.push_back(ptr);

            // Metrics: Memory deallocated
            common::Metrics::GaugeDec(common::MetricsStd::MemPoolAllocatedBlocks);
            common::Metrics::GaugeInc(common::MetricsStd::MemPoolFreeBlocks);
            common::Metrics::CounterInc(common::MetricsStd::MemPoolDeallocations);

            if (free_mem_vec_.size() > kMaxBlockNum) {
                // Safe to call ReleaseHalf - we're in the owning thread
                ReleaseHalf();
            }
        });
        m = nullptr;  // Clear caller's pointer
        return;
    }

    // No event loop (testing or thread shutdown) - return to pool directly
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