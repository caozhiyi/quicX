#include "common/buffer/buffer_read.h"
#include "common/alloter/pool_block.h"

namespace quicx {

BufferRead::BufferRead(uint8_t* data, uint8_t* end, std::shared_ptr<BlockMemoryPool>& alloter):
    BufferReadView(data, end),
    _alloter(alloter) {

}

BufferRead::~BufferRead() {
    if (_buffer_start) {
        auto alloter = _alloter.lock();
        if (alloter) {
            void* m = (void*)_buffer_start;
            alloter->PoolLargeFree(m);
        }
    }
}

}