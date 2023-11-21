// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_ALLOTER_POOL_ALLOTER
#define COMMON_ALLOTER_POOL_ALLOTER

#include <vector>
#include <cstdint>
#include "alloter_interface.h"

namespace quicx {
namespace common {

static const uint32_t __default_max_bytes = 256;
static const uint32_t __default_number_of_free_lists = __default_max_bytes / __align;
static const uint32_t __default_number_add_nodes = 20;

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
    uint32_t FreeListIndex(uint32_t size, uint32_t align = __align) {
        return (size + align - 1) / align - 1;
    }
    
    void* ReFill(uint32_t size, uint32_t num = __default_number_add_nodes);
    void* ChunkAlloc(uint32_t size, uint32_t& nums);

private:
    union MemNode {
        MemNode*    _next;
        uint8_t     _data[1];
    };
    
    uint8_t*  _pool_start;         
    uint8_t*  _pool_end;
    std::vector<MemNode*>     _free_list;  
    std::vector<uint8_t*>        _malloc_vec;
    std::shared_ptr<IAlloter> _alloter;
};

std::shared_ptr<IAlloter> MakePoolAlloterPtr();

}
}

#endif 