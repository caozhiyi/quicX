#include <vector>

#include "common/buffer/if_buffer_chunk.h"
#include "common/buffer/single_block_buffer.h"
#include "common/buffer/single_block_buffer_pool.h"

namespace quicx {
namespace common {

namespace {

// Hard cap to avoid unbounded growth; mirrors BufferChunkPool's budget. A
// cached entry is just the wrapper object (~64 B): chunks are detached at
// recycle time, so nothing pins packet memory while parked.
constexpr std::size_t kFreeListCap = 256;

struct ThreadLocalFreeList {
    std::vector<SingleBlockBuffer*> buffers;

    ~ThreadLocalFreeList() {
        for (auto* b : buffers) {
            delete b;
        }
        buffers.clear();
    }
};

// Function-local static: construction deferred until first use and the symbol
// stays hidden inside this TU (same pattern as buffer_chunk_pool.cpp).
ThreadLocalFreeList& Tls() {
    thread_local ThreadLocalFreeList tls;
    return tls;
}

void Recycle(SingleBlockBuffer* raw) {
    if (raw == nullptr) {
        return;
    }
    // Drop the backing chunk first so it returns to the BufferChunkPool
    // immediately. Reset() clears the derived pointers (null chunk is
    // handled) and preserves capacity_limit_ by design.
    raw->Reset(nullptr);
    auto& tls = Tls();
    if (tls.buffers.size() >= kFreeListCap) {
        delete raw;
        return;
    }
    tls.buffers.push_back(raw);
}

}  // namespace

std::shared_ptr<SingleBlockBuffer> SingleBlockBufferPool::Acquire(std::shared_ptr<IBufferChunk> chunk) {
    if (!chunk) {
        return nullptr;
    }

    auto& tls = Tls();
    SingleBlockBuffer* raw = nullptr;
    if (!tls.buffers.empty()) {
        raw = tls.buffers.back();
        tls.buffers.pop_back();
        // Rebind to the fresh chunk; this re-runs InitializePointers() and
        // re-clamps buffer_end_ against the (preserved) capacity_limit_.
        raw->Reset(std::move(chunk));
    } else {
        raw = new SingleBlockBuffer(std::move(chunk));
    }
    return std::shared_ptr<SingleBlockBuffer>(raw, &Recycle);
}

}  // namespace common
}  // namespace quicx
