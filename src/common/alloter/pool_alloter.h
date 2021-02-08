#ifndef COMMON_POOL_ALLOTER
#define COMMON_POOL_ALLOTER

#include <vector>
#include <cstdint>
#include "alloter_interface.h"

namespace quicx {

static const uint32_t __default_max_bytes = 256;
static const uint32_t __default_number_of_free_lists = __default_max_bytes / __align;
static const uint32_t __default_number_add_nodes = 20;

class PoolAlloter : public Alloter {
public:
    PoolAlloter(uint32_t large_sz, uint32_t add_num);
    ~PoolAlloter();

    void* Malloc(uint32_t size);
    void* MallocAlign(uint32_t size);
    void* MallocZero(uint32_t size);

    void Free(char* &data, uint32_t len);
private:
    uint32_t FreeListIndex(uint32_t size, uint32_t align = __align) {
        return (size + align - 1) / align - 1;
    }
    
    void* ReFill(uint32_t size, uint32_t num = __default_number_add_nodes);
    void* ChunkAlloc(uint32_t size, uint32_t& nums);

private:
    union MemNode {
        MemNode*    _next;
        char        _data[1];
    };
    
    char*  _pool_start;         
    char*  _pool_end;
    std::vector<MemNode*> _free_list;  
    std::vector<char*>    _malloc_vec;
};

}

#endif 