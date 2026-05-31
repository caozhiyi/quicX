#ifndef COMMON_BUFFER_BUFFER_CHUNK_POOL
#define COMMON_BUFFER_BUFFER_CHUNK_POOL

#include <memory>

namespace quicx {
namespace common {

class BufferChunk;
class BlockMemoryPool;

// Thread-local object pool for BufferChunk.
//
// Background:
//   Hot paths (per-packet plaintext buffers in the QUIC decoder) construct a
//   fresh BufferChunk for every datagram, which produces ~167K
//   BufferChunk(pool) ctor invocations per 50MB download (and a matching
//   number of shared_ptr control-block allocations).
//
//   The underlying memory blocks are already pooled by BlockMemoryPool, but
//   the BufferChunk *object* itself (and its shared_ptr aliasing control
//   block) is freshly allocated each time. This pool recycles the BufferChunk
//   wrapper so the ctor work and one heap allocation are amortised.
//
// Lifetime / safety:
//   - Acquire() returns a std::shared_ptr<BufferChunk>. When the last
//     shared_ptr (and therefore every SharedBufferSpan / SingleBlockBuffer
//     that referenced this chunk) is destroyed, a custom deleter returns the
//     raw object to a per-thread free list instead of deleting it.
//   - Because deletion only fires after every reference is released, the
//     chunk's freeze_count_ is guaranteed to be 0 by then; no manual reset is
//     required (FreezeUpTo/Unfreeze auto-clear on the last unfreeze).
//   - The free list is thread-local: Acquire and the deleter both run on the
//     same QUIC worker thread, so no synchronisation is needed.
//   - On thread exit the free list destructor deletes any cached chunks,
//     which routes their owned blocks back to BlockMemoryPool through the
//     usual BufferChunk::~BufferChunk path.
class BufferChunkPool {
public:
    // Returns a pooled BufferChunk wrapped in a shared_ptr whose deleter
    // recycles the object instead of freeing it. If the free list is empty
    // a new BufferChunk is allocated normally; subsequent releases will
    // populate the cache.
    //
    // Returns nullptr only if `pool` is null OR allocation fails (matches
    // the failure mode of BufferChunk's ctor: an "invalid" chunk is
    // returned, just like the direct make_shared path would yield).
    static std::shared_ptr<BufferChunk> Acquire(
        const std::shared_ptr<BlockMemoryPool>& pool);
};

}  // namespace common
}  // namespace quicx

#endif
