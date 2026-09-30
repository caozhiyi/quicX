#ifndef COMMON_BUFFER_SINGLE_BLOCK_BUFFER_POOL
#define COMMON_BUFFER_SINGLE_BLOCK_BUFFER_POOL

#include <memory>

namespace quicx {
namespace common {

class SingleBlockBuffer;
class IBufferChunk;

// Thread-local object pool for SingleBlockBuffer — the per-packet egress
// IBuffer wrapper on the QUIC send path.
//
// Background:
//   EmitOnePacket used to make_shared a fresh SingleBlockBuffer for every
//   outgoing packet (object + shared_ptr control block, on top of the pooled
//   BufferChunk). At ~66k pps those two wrapper allocations showed up as
//   ~17% of the sender worker's CPU (malloc + _int_malloc + free in perf).
//   This pool recycles the wrapper object the same way BufferChunkPool
//   recycles chunks.
//
// Lifetime / safety:
//   - Acquire() returns a shared_ptr whose deleter detaches the backing chunk
//     (Reset(nullptr)) and parks the wrapper on a per-thread free list
//     instead of deleting it.
//   - Detaching at recycle time releases the chunk immediately (back to the
//     BufferChunkPool through its own deleter), so parked wrappers pin no
//     packet memory.
//   - The deleter only fires after the last shared_ptr / span reference
//     drops; for the batched send path that is the tx_batch.clear() after the
//     drain-round sendmmsg flush, and retransmission bookkeeping can only
//     delay recycling, never break it.
//   - capacity_limit_ intentionally survives recycling (it is a view
//     property kept across Reset() by design; the send path re-applies its
//     cap on every packet anyway).
//   - Free list is thread-local: Acquire() and the deleter both run on the
//     same QUIC worker thread, so no synchronisation is needed.
class SingleBlockBufferPool {
public:
    // Bind `chunk` to a pooled SingleBlockBuffer and return it wrapped in a
    // recycling shared_ptr. Returns nullptr only if `chunk` is null.
    static std::shared_ptr<SingleBlockBuffer> Acquire(std::shared_ptr<IBufferChunk> chunk);
};

}  // namespace common
}  // namespace quicx

#endif  // COMMON_BUFFER_SINGLE_BLOCK_BUFFER_POOL
