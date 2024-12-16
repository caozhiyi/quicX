// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#include <cstring>
#include <cstdlib>

#include "common/alloter/pool_alloter.h"
#include "common/alloter/normal_alloter.h"

namespace quicx {
namespace common {

PoolAlloter::PoolAlloter() : 
    pool_start_(nullptr),
    pool_end_(nullptr) {
    free_list_.resize(__default_number_of_free_lists);
    memset(&(*free_list_.begin()), 0, sizeof(void*) * __default_number_of_free_lists);
    alloter_ = MakeNormalAlloterPtr();
}

PoolAlloter::~PoolAlloter() {
    for (auto iter = malloc_vec_.begin(); iter != malloc_vec_.end(); ++iter) {
        if (*iter) {
            void* data = (void*)*iter;
            alloter_->Free(data);
        }
    }
}

void* PoolAlloter::Malloc(uint32_t size) {
    if (size > __default_max_bytes) {
        void* ret = alloter_->Malloc(size);
        return ret;
    }

    MemNode** my_free = &(free_list_[FreeListIndex(size)]);
    MemNode* result = *my_free;
    if (result == nullptr) {
        void* bytes = ReFill(Align(size));
        return bytes;
    }
    
    *my_free = result->next_;
    return result;
}

void* PoolAlloter::MallocAlign(uint32_t size) {
    return Malloc(Align(size));
}

void* PoolAlloter::MallocZero(uint32_t size) {
    void* ret = Malloc(size);
    if (ret) {
        memset(ret, 0, size);
    }
    return ret;
}

void PoolAlloter::Free(void* &data, uint32_t len) {
    if (!data) {
        return;
    }
    
    if (len > __default_max_bytes) {
        alloter_->Free(data);
        data = nullptr;
        return;
    }

    MemNode* node = (MemNode*)data;
    MemNode** my_free = &(free_list_[FreeListIndex(len)]);
    
    node->next_ = *my_free;
    *my_free = node;
    data = nullptr;
}

void* PoolAlloter::ReFill(uint32_t size, uint32_t num) {
    uint32_t nums = num;

    uint8_t* chunk = (uint8_t*)ChunkAlloc(size, nums);

    MemNode* volatile* my_free;
    MemNode* res, *current, *next;
    if (1 == nums) {
        return chunk;
    }

    res = (MemNode*)chunk;
    
    my_free = &(free_list_[FreeListIndex(size)]);

    *my_free = next = (MemNode*)(chunk + size);
    for (uint32_t i = 1;; i++) {
        current = next;
        next = (MemNode*)((uint8_t*)next + size);
        if (nums - 1 == i) {
            current->next_ = nullptr;
            break;

        } else {
            current->next_ = next;
        }
    }
    return res;
}

void* PoolAlloter::ChunkAlloc(uint32_t size, uint32_t& nums) {
    uint8_t* res;
    uint32_t need_bytes = size * nums;
    uint32_t left_bytes = uint32_t(pool_end_ - pool_start_);

    //pool is enough
    if (left_bytes >= need_bytes) {
        res = pool_start_;
        pool_start_ += need_bytes;
        return res;
    
    } else if (left_bytes >= size) {
        nums = left_bytes / size;
        need_bytes = size * nums;
        res = pool_start_;
        pool_start_ += need_bytes;
        return res;

    } 
    uint32_t bytes_to_get = size * nums;

    if (left_bytes > 0) {
        MemNode* my_free = free_list_[FreeListIndex(left_bytes)];
        ((MemNode*)pool_start_)->next_ = my_free;
        free_list_[FreeListIndex(size)] = (MemNode*)pool_start_;
    }

    pool_start_ = (uint8_t*)alloter_->Malloc(bytes_to_get);

    malloc_vec_.push_back(pool_start_);
    pool_end_ = pool_start_ + bytes_to_get;
    return ChunkAlloc(size, nums);
}

std::shared_ptr<IAlloter> MakePoolAlloterPtr() {
    return std::make_shared<PoolAlloter>();
}

}
}