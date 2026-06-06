#include <vector>

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_chunk.h"
#include "common/buffer/buffer_chunk_pool.h"

namespace quicx {
namespace common {

namespace {

// Hard cap to avoid unbounded growth if a producer outpaces consumption.
// Each cached entry holds one BlockMemoryPool block (1500B by default) plus
// the BufferChunk object itself.
constexpr std::size_t kFreeListCap = 256;

// Per-thread free list of recycled BufferChunk objects. Each entry still owns
// its pool-managed block (BufferChunk's dtor would normally return it to the
// BlockMemoryPool). When the thread exits, the destructor of this vector
// runs and deletes every cached BufferChunk, which routes blocks back to
// BlockMemoryPool through BufferChunk::~BufferChunk.
struct ThreadLocalFreeList {
    std::vector<BufferChunk*> chunks;

    ~ThreadLocalFreeList() {
        for (auto* c : chunks) {
            delete c;
        }
        chunks.clear();
    }
};

// Defined as a function-local static instead of a translation-unit-level
// thread_local to defer construction until first use AND to keep the symbol
// hidden inside this TU.
ThreadLocalFreeList& Tls() {
    thread_local ThreadLocalFreeList tls;
    return tls;
}

void Recycle(BufferChunk* raw) {
    if (raw == nullptr) {
        return;
    }
    // If the chunk is no longer valid (e.g. its pool was destroyed and
    // BufferChunk::Release() has freed the block), there is nothing to recycle:
    // the wrapper has lost its block and must be deleted normally.
    if (!raw->Valid()) {
        delete raw;
        return;
    }
    auto& tls = Tls();
    if (tls.chunks.size() >= kFreeListCap) {
        // Cache is full; just delete (which returns the block to BlockMemoryPool
        // via BufferChunk::Release()).
        delete raw;
        return;
    }
    // Note: by the time the deleter fires, every SharedBufferSpan that
    // referenced this chunk has already been destroyed, so freeze_count_ is
    // guaranteed to be 0 and write_floor_offset_ is guaranteed to be 0
    // (auto-cleared by the last Unfreeze()). No manual reset is required.
    tls.chunks.push_back(raw);
}

}  // namespace

std::shared_ptr<BufferChunk> BufferChunkPool::Acquire(
        const std::shared_ptr<BlockMemoryPool>& pool) {
    if (!pool) {
        return nullptr;
    }

    auto& tls = Tls();
    BufferChunk* raw = nullptr;
    if (!tls.chunks.empty()) {
        raw = tls.chunks.back();
        tls.chunks.pop_back();
    } else {
        raw = new BufferChunk(pool);
        if (!raw->Valid()) {
            // ctor logged the failure already; surface as nullptr so callers
            // can short-circuit just like they did with the legacy
            // std::make_shared<BufferChunk> path (which would also yield an
            // invalid chunk).
            delete raw;
            return nullptr;
        }
    }
    // Wrap with a custom deleter that returns the raw to the free list.
    // shared_ptr's control block is the only remaining per-call heap
    // allocation in this path; it is unavoidable without a more invasive
    // intrusive_ptr refactor.
    return std::shared_ptr<BufferChunk>(raw, &Recycle);
}

}  // namespace common
}  // namespace quicx
