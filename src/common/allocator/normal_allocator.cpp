#include "common/allocator/normal_allocator.h"
#include <cstdlib>
#include <cstring>  //for memset
#include <new>

namespace quicx {
namespace common {

NormalAllocator::NormalAllocator() {}

NormalAllocator::~NormalAllocator() {}

void* NormalAllocator::Malloc(uint32_t size) {
    void* ret = malloc((size_t)size);
    if (!ret) {
        throw std::bad_alloc();
    }

    return ret;
}

void* NormalAllocator::MallocAlign(uint32_t size) {
    return Malloc(Align(size));
}

void* NormalAllocator::MallocZero(uint32_t size) {
    void* ret = Malloc(size);
    if (ret) {
        memset(ret, 0, size);
    }
    return ret;
}

void NormalAllocator::Free(void*& data, uint32_t len) {
    free(data);
    data = nullptr;
}

std::shared_ptr<NormalAllocator> MakeNormalAllocatorPtr() {
    return std::make_shared<NormalAllocator>();
}

}  // namespace common
}  // namespace quicx