#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_write.h"

namespace quicx {

BufferWrite::BufferWrite(uint8_t* data, uint8_t* end, std::shared_ptr<BlockMemoryPool>& alloter):
    BufferWriteView(data, end),
    _alloter(alloter) {

}

BufferWrite::~BufferWrite() {
    if (_buffer_start) {
        auto alloter = _alloter.lock();
        if (alloter) {
            void* m = (void*)_buffer_start;
            alloter->PoolLargeFree(m);
        }
    }
}

}