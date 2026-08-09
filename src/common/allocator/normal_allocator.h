#ifndef COMMON_ALLOCATOR_NORMAL_ALLOCATOR
#define COMMON_ALLOCATOR_NORMAL_ALLOCATOR

#include "common/allocator/if_allocator.h"

namespace quicx {
namespace common {

class NormalAllocator: public IAllocator {
public:
    NormalAllocator();
    ~NormalAllocator();

    void* Malloc(uint32_t size);
    void* MallocAlign(uint32_t size);
    void* MallocZero(uint32_t size);

    void Free(void*& data, uint32_t len = 0);
};

std::shared_ptr<NormalAllocator> MakeNormalAllocatorPtr();

}  // namespace common
}  // namespace quicx

#endif