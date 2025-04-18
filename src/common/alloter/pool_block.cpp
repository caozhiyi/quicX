// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#include <cstdlib>
#include <algorithm>
#include "common/alloter/pool_block.h"

namespace quicx {
namespace common {

static const uint16_t kMaxBlockNum = 20;

BlockMemoryPool::BlockMemoryPool(uint32_t large_sz, uint32_t add_num) :
    number_large_add_nodes_(add_num),
    large_size_(large_sz) {

}

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
    return ret;
}

void BlockMemoryPool::PoolLargeFree(void* &m) {
    free_mem_vec_.push_back(m);
    if (free_mem_vec_.size() > kMaxBlockNum) {
        // release some block.
        ReleaseHalf();
    }
}

uint32_t BlockMemoryPool::GetSize() {
    return (uint32_t)free_mem_vec_.size();
}

uint32_t BlockMemoryPool::GetBlockLength() {
    return large_size_;
}

void BlockMemoryPool::ReleaseHalf() {
    size_t size = free_mem_vec_.size();
    size_t hale = size / 2;
    for (auto iter = free_mem_vec_.begin(); iter != free_mem_vec_.end();) {
        void* mem = *iter;

        iter = free_mem_vec_.erase(iter);
        free(mem);
        
        size--;
        if (iter == free_mem_vec_.end() || size <= hale) {
            break;
        }
    }
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
}

std::shared_ptr<common::BlockMemoryPool> MakeBlockMemoryPoolPtr(uint32_t large_sz, uint32_t add_num) {
    return std::make_shared<BlockMemoryPool>(large_sz, add_num);
}

}
}