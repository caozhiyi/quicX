#ifndef COMMON_ALLOTER_POOL_ALLOTER
#define COMMON_ALLOTER_POOL_ALLOTER

#include <vector>
#include <cstdint>
#include "common/alloter/if_alloter.h"

namespace quicx {
namespace common {

static const uint32_t kDefaultMaxBytes = 256;
static const uint32_t kDefaultNumberOfFreeLists = kDefaultMaxBytes / kAlign;
static const uint32_t kDefaultNumberAddNodes = 20;

/**
 * @brief Slab-style pool allocator for small objects (<= kDefaultMaxBytes).
 *
 * Thread-safety contract: **this allocator is intentionally NOT thread-safe.**
 * It is designed to be owned by a single logical owner (typically one
 * Connection, which the framework pins to a single I/O thread). All
 * Malloc/Free/MallocAlign/MallocZero calls must happen on that owning
 * thread. Cross-thread access is a data race.
 *
 * Sizing:
 *   - size <= kDefaultMaxBytes (default 256B) -> served from free-list / chunk.
 *   - size >  kDefaultMaxBytes                -> falls through to the backing
 *                                                 NormalAlloter (plain malloc).
 *
 * Lifetime:
 *   - Chunks are only released in ~PoolAlloter(). The pool is monotonically
 *     growing during its lifetime, which matches "one pool per connection":
 *     the whole arena is reclaimed when the connection ends.
 */
class PoolAlloter:
    public IAlloter {
public:
    PoolAlloter();
    ~PoolAlloter();

    void* Malloc(uint32_t size);
    void* MallocAlign(uint32_t size);
    void* MallocZero(uint32_t size);

    void Free(void* &data, uint32_t len);
private:
    uint32_t FreeListIndex(uint32_t size, uint32_t align = kAlign) {
        return (size + align - 1) / align - 1;
    }
    
    void* ReFill(uint32_t size, uint32_t num = kDefaultNumberAddNodes);
    void* ChunkAlloc(uint32_t size, uint32_t& nums);

private:
    union MemNode {
        MemNode*    next_;
        uint8_t     data_[1];
    };
    
    uint8_t*  pool_start_;         
    uint8_t*  pool_end_;
    std::vector<MemNode*>     free_list_;  
    std::vector<uint8_t*>     malloc_vec_;
    std::shared_ptr<IAlloter> alloter_;
};

std::shared_ptr<IAlloter> MakePoolAlloterPtr();

}
}

#endif 