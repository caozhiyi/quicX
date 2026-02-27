#ifndef COMMON_ALLOTER_NORMAL_ALLOTER
#define COMMON_ALLOTER_NORMAL_ALLOTER

#include "common/alloter/if_alloter.h"

namespace quicx {
namespace common {

class NormalAlloter:
    public IAlloter {
public:
    NormalAlloter();
    ~NormalAlloter();

    void* Malloc(uint32_t size);
    void* MallocAlign(uint32_t size);
    void* MallocZero(uint32_t size);

    void Free(void* &data, uint32_t len = 0);
};

std::shared_ptr<NormalAlloter> MakeNormalAlloterPtr();

}
}

#endif 