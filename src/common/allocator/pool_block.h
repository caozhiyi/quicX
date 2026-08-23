#ifndef COMMON_ALLOCATOR_POOL_BLOCK
#define COMMON_ALLOCATOR_POOL_BLOCK

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace quicx {
namespace common {

// all memory must return memory pool before destroy.
//
// Thread-affine bulk memory pool.
//
// Each pool is owned by exactly one thread (the one that performs the common,
// same-thread PoolLargeMalloc / PoolLargeFree). The free list (free_mem_vec_)
// is therefore only ever mutated by the owner thread and needs no lock.
//
// When a block is returned to the pool from a *different* thread it cannot be
// pushed straight onto free_mem_vec_ (that would be a data race), so it is
// pushed onto a lock-free intrusive handback stack (handback_head_). The owner
// thread reclaims those blocks via DrainHandback() — called from PoolLargeMalloc
// and the destructor — and folds them back into free_mem_vec_. This keeps the
// hot same-thread path lock-free and allocation-free, while the cross-thread
// path is a single atomic CAS with no heap allocation: strictly cheaper than the
// old event-loop PostTask hop, which allocated a std::function + atomic weak_ptr
// on every cross-thread free.
class BlockMemoryPool: public std::enable_shared_from_this<BlockMemoryPool> {
public:
    // bulk memory size.
    // every time add nodes num
    BlockMemoryPool(uint32_t large_sz, uint32_t add_num);
    virtual ~BlockMemoryPool();

    // for bulk memory.
    // return one bulk memory node
    virtual void* PoolLargeMalloc();
    virtual void PoolLargeFree(void*& m);

    // return bulk memory list size
    virtual uint32_t GetSize();
    // return length of bulk memory
    virtual uint32_t GetBlockLength();

    // Mark the thread that owns this pool. Only the owner thread may mutate
    // free_mem_vec_ directly; cross-thread frees are deferred to handback_head_.
    // Replaces SetEventLoop(): the pool no longer needs to know about any event
    // loop, which also fixes the case where the packet allocator's pool was never
    // given an event loop and therefore took the unsafe fast path.
    virtual void SetOwnerThread(std::thread::id owner);

    // release half memory
    virtual void ReleaseHalf();
    virtual void Expansion(uint32_t num = 0);

private:
    // Owner thread: fold deferred cross-thread frees back into free_mem_vec_.
    void DrainHandback();

    uint32_t number_large_add_nodes_;       // every time add nodes num
    uint32_t large_size_;                   // bulk memory size
    // Protects free_mem_vec_. The original single-owner design is broken by
    // pools that travel across threads: interop_client creates streams (and
    // their MultiBlockBuffers, which capture this pool) on the main thread,
    // while worker threads call EnsureWritableChunk() -> PoolLargeMalloc() on
    // the same pool. Both the foreign malloc (DrainHandback + pop) and the
    // owner's push/pop then raced on the vector, duplicating pointers and
    // ultimately double-freeing blocks (ASan-confirmed). All vector accesses
    // now run under this lock; the lock-free handback stack is kept so that
    // cross-thread frees stay off the lock entirely.
    mutable std::mutex free_vec_mtx_;
    std::vector<void*> free_mem_vec_;       // free bulk memory list
    // Thread allowed to touch free_mem_vec_ without going through the
    // handback stack (it still takes free_vec_mtx_). MUST be atomic: foreign
    // threads read it in PoolLargeFree() concurrently with the owner writing
    // it in SetOwnerThread(). A plain std::thread::id is a data race, and a
    // stale read of the default-constructed id made foreign threads take the
    // "owner not yet known" fast path and push directly onto free_mem_vec_,
    // corrupting the heap (glibc "corrupted double-linked list" seen under
    // multiplexing workloads when the main thread tears down 1999 streams).
    static_assert(std::is_trivially_copyable_v<std::thread::id>,
                  "std::atomic<std::thread::id> requires a trivially copyable id type");
    std::atomic<std::thread::id> owner_tid_{};   // thread allowed to touch free_mem_vec_
    std::atomic<void*> handback_head_{nullptr};  // cross-thread free stack (intrusive)
};

std::shared_ptr<common::BlockMemoryPool> MakeBlockMemoryPoolPtr(uint32_t large_sz, uint32_t add_num);

}  // namespace common
}  // namespace quicx

#endif