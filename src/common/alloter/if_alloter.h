#ifndef COMMON_ALLOTER_IF_ALLOTER
#define COMMON_ALLOTER_IF_ALLOTER

#include <memory>
#include <new>
#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace quicx {
namespace common {

static const uint16_t kAlign = sizeof(unsigned long);

/**
 * @brief Memory allocator interface
 *
 * Base interface for custom memory allocation strategies.
 */
class IAlloter {
public:
    IAlloter() {}
    virtual ~IAlloter() {}

    /**
     * @brief Allocate memory
     *
     * @param size Size in bytes
     * @return Pointer to allocated memory, or nullptr on failure
     */
    virtual void* Malloc(uint32_t size) = 0;

    /**
     * @brief Allocate aligned memory
     *
     * @param size Size in bytes
     * @return Pointer to aligned memory, or nullptr on failure
     */
    virtual void* MallocAlign(uint32_t size) = 0;

    /**
     * @brief Allocate zero-initialized memory
     *
     * @param size Size in bytes
     * @return Pointer to zero-filled memory, or nullptr on failure
     */
    virtual void* MallocZero(uint32_t size) = 0;

    /**
     * @brief Free allocated memory
     *
     * @param data Pointer to memory to free (set to nullptr after freeing)
     * @param len Size of memory block in bytes (optional)
     */
    virtual void Free(void* &data, uint32_t len = 0) = 0;

protected:
    /**
     * @brief Align size to platform word boundary
     *
     * @param size Unaligned size
     * @return Aligned size
     */
    uint32_t Align(uint32_t size) {
        return ((size + kAlign - 1) & ~(kAlign - 1));
    }
};

/**
 * @brief Stateless deleter for pool-allocated objects.
 *
 * Holds only a raw IAlloter* (one pointer, no atomic refcount). Intended to be
 * used as the Deleter for std::unique_ptr, producing a zero-overhead RAII
 * wrapper around pool-allocated objects.
 *
 * Lifetime contract: the referenced IAlloter MUST outlive every
 * PoolUniquePtr<T> it produced. In the "one PoolAlloter per Connection"
 * model this is naturally true because the alloter is owned by the
 * connection and destroyed last.
 */
template<typename T>
class PoolDeleter {
public:
    PoolDeleter() noexcept : alloter_(nullptr) {}
    explicit PoolDeleter(IAlloter* a) noexcept : alloter_(a) {}

    void operator()(T* p) const noexcept {
        if (!p) return;
        p->~T();
        void* data = static_cast<void*>(p);
        alloter_->Free(data, sizeof(T));
    }

    IAlloter* GetAlloter() const noexcept { return alloter_; }

private:
    IAlloter* alloter_;
};

/**
 * @brief Unique-ownership smart pointer for pool-allocated objects.
 *
 * Same cost as a raw pointer + manual PoolDelete, but RAII-safe. Prefer this
 * over PoolNewSharePtr on any hot path — the latter still allocates the
 * shared_ptr control block on the default heap and incurs atomic refcount
 * operations.
 */
template<typename T>
using PoolUniquePtr = std::unique_ptr<T, PoolDeleter<T>>;

/**
 * @brief std::allocator-compatible adapter over IAlloter.
 *
 * Plug into std::allocate_shared<T>(PoolStdAllocator<T>(alloter), args...)
 * so that BOTH the shared_ptr control block AND the object are allocated
 * from the pool in a single placement-new (the same fast path that
 * std::make_shared uses against the default heap). This eliminates the
 * separate operator new for the control block that PoolNewSharePtr suffers
 * from.
 *
 * Lifetime contract: the referenced IAlloter MUST outlive the resulting
 * shared_ptr AND any weak_ptr observing it. In the "one PoolAlloter per
 * Connection" model this is naturally true because the alloter is owned by
 * the connection and the connection outlives every frame/object it owns.
 *
 * Sizing caveat: the combined block is sizeof(T) + ~32-48B of control-block
 * metadata. If that total exceeds PoolAlloter::kDefaultMaxBytes (256B), the
 * allocation transparently falls through to the backing NormalAlloter
 * (plain malloc); behaviour stays correct, but the pool benefit is lost
 * for that object.
 *
 * Thread-safety: inherits the underlying IAlloter's contract. PoolAlloter
 * is single-threaded; passing a shared_ptr produced this way across
 * threads is safe for reading but the final destruction MUST happen on
 * the alloter's owning thread.
 */
template<typename T>
class PoolStdAllocator {
public:
    using value_type = T;
    // Required by std::allocator_traits for stateful allocators interoperating
    // with std::allocate_shared (some libstdc++ versions still query these).
    using propagate_on_container_copy_assignment = std::false_type;
    using propagate_on_container_move_assignment = std::false_type;
    using propagate_on_container_swap            = std::false_type;
    using is_always_equal                        = std::false_type;

    template<typename U> struct rebind { using other = PoolStdAllocator<U>; };

    PoolStdAllocator() noexcept : alloter_(nullptr) {}
    explicit PoolStdAllocator(IAlloter* a) noexcept : alloter_(a) {}

    template<typename U>
    PoolStdAllocator(const PoolStdAllocator<U>& other) noexcept
        : alloter_(other.GetAlloter()) {}

    T* allocate(std::size_t n) {
        // PoolAlloter::Free needs the size, and the STL contract guarantees the
        // matching deallocate(p, n) is called with the same n. So we don't have
        // to remember the size out-of-band.
        const std::size_t bytes = n * sizeof(T);
        void* p = alloter_->MallocAlign(static_cast<uint32_t>(bytes));
        if (!p) {
            // std::allocate_shared requires throwing on failure.
            throw std::bad_alloc();
        }
        return static_cast<T*>(p);
    }

    void deallocate(T* p, std::size_t n) noexcept {
        if (!p) return;
        void* data = static_cast<void*>(p);
        alloter_->Free(data, static_cast<uint32_t>(n * sizeof(T)));
    }

    IAlloter* GetAlloter() const noexcept { return alloter_; }

    template<typename U>
    bool operator==(const PoolStdAllocator<U>& o) const noexcept {
        return alloter_ == o.GetAlloter();
    }
    template<typename U>
    bool operator!=(const PoolStdAllocator<U>& o) const noexcept {
        return alloter_ != o.GetAlloter();
    }

private:
    IAlloter* alloter_;
};

/**
 * @brief Wrapper for pool-based object and memory allocation
 *
 * Provides convenient methods for allocating objects and memory from a pool.
 *
 * Preferred API for hot paths (in descending order of performance):
 *   1. PoolNew<T>(...) + PoolDelete<T>(p)   - raw pointer, manual lifetime.
 *   2. PoolMakeUnique<T>(...)               - RAII via unique_ptr, same perf.
 *   3. PoolMakeShared<T>(...)               - RAII via shared_ptr; control
 *                                              block + object pooled in a
 *                                              single allocation.
 *   4. PoolNewSharePtr<T>(...)              - DEPRECATED, see its doc.
 */
class AlloterWrap {
public:
    AlloterWrap(std::shared_ptr<IAlloter> a) : alloter_(a) {}
    ~AlloterWrap() {}

    /**
     * @brief Allocate and construct an object from the pool (raw pointer).
     *
     * The caller is responsible for lifetime and MUST call PoolDelete<T>()
     * on the returned pointer. For automatic lifetime management prefer
     * PoolMakeUnique<T>().
     *
     * @tparam T Object type
     * @tparam Args Constructor argument types
     * @param args Constructor arguments
     * @return Pointer to constructed object, or nullptr on failure
     */
    template<typename T, typename... Args >
    T* PoolNew(Args&&... args);

    /**
     * @brief Allocate and construct an object, returning a PoolUniquePtr<T>.
     *
     * Zero-overhead RAII: the deleter is a single raw IAlloter* (no control
     * block, no atomic refcount, no captured shared_ptr). This is the
     * preferred replacement for PoolNewSharePtr on hot paths.
     *
     * @tparam T Object type
     * @tparam Args Constructor argument types
     * @param args Constructor arguments
     * @return PoolUniquePtr<T>, empty on failure
     */
    template<typename T, typename... Args>
    PoolUniquePtr<T> PoolMakeUnique(Args&&... args);

    /**
     * @brief Allocate and construct an object, returning a shared_ptr whose
     *        control block is also pooled.
     *
     * Internally calls std::allocate_shared with PoolStdAllocator<T>, which
     * lets the standard library allocate the control block and the T object
     * in a SINGLE call to the pool (the same fast path std::make_shared
     * uses for the default heap). Compared to PoolNewSharePtr this saves:
     *   - one separate operator new for the control block (~30 ns);
     *   - the deleter capture of std::shared_ptr<IAlloter> and its atomic
     *     inc/dec on every copy/destruction.
     *
     * Use this only when shared ownership is actually needed; otherwise
     * prefer PoolMakeUnique<T>() (cheaper) or PoolNew<T>() + PoolDelete<T>().
     *
     * Caveat: sizeof(T) + control block (~40B) must fit under
     * PoolAlloter::kDefaultMaxBytes (256B) to stay in the free-list path;
     * larger combined sizes transparently fall through to the backing
     * NormalAlloter (correct, but not pooled).
     *
     * @tparam T Object type
     * @tparam Args Constructor argument types
     * @param args Constructor arguments
     * @return std::shared_ptr<T>; throws std::bad_alloc on allocation failure
     *         (per std::allocate_shared contract)
     */
    template<typename T, typename... Args>
    std::shared_ptr<T> PoolMakeShared(Args&&... args);

    /**
     * @brief Allocate and construct an object, returning a shared_ptr.
     *
     * @deprecated Superseded by PoolMakeShared<T>(), which pools the
     * shared_ptr control block as well. This older API:
     *   (1) allocates the control block via the default operator new
     *       (NOT the pool);
     *   (2) the deleter captures std::shared_ptr<IAlloter>, adding atomic
     *       inc/dec on construction/destruction;
     *   (3) on most STL implementations the type-erased deleter inflates
     *       the control block (~48-64B).
     *
     * For hot paths prefer PoolMakeUnique<T>() (zero overhead, RAII) or
     * PoolNew<T>() + manual PoolDelete<T>(). When shared ownership is
     * required, use PoolMakeShared<T>() instead of this function.
     */
    template<typename T, typename... Args >
    [[deprecated("Slow on hot paths: shared_ptr control block is not pooled. "
                 "Use PoolMakeShared<T>() (pooled control block), "
                 "PoolMakeUnique<T>() (zero-overhead RAII), or "
                 "PoolNew<T>() + PoolDelete<T>() instead.")]]
    std::shared_ptr<T> PoolNewSharePtr(Args&&... args);

    /**
     * @brief Destroy and deallocate a pool-allocated object
     *
     * @tparam T Object type
     * @param c Pointer to object to delete
     */
    template<typename T>
    void PoolDelete(T* c);

    /**
     * @brief Allocate continuous memory from the pool
     *
     * @tparam T Element type
     * @param size Size in bytes
     * @return Pointer to allocated memory, or nullptr on failure
     */
    template<typename T>
    T* PoolMalloc(uint32_t size);

    /**
     * @brief Allocate continuous memory, returning a shared_ptr
     *
     * @deprecated Same overhead concerns as PoolNewSharePtr: the control
     * block is allocated outside the pool. Prefer PoolMalloc<T>() +
     * PoolFree<T>() for raw buffers; if shared ownership of a typed object
     * is needed use PoolMakeShared<T>() instead.
     */
    template<typename T>
    [[deprecated("Slow on hot paths: shared_ptr control block is not pooled. "
                 "Use PoolMalloc<T>() + PoolFree<T>() instead.")]]
    std::shared_ptr<T> PoolMallocSharePtr(uint32_t size);

    /**
     * @brief Free pool-allocated continuous memory
     *
     * @tparam T Element type
     * @param m Pointer to memory to free
     * @param len Size in bytes
     */
    template<typename T>
    void PoolFree(T* m, uint32_t len);

    /**
     * @brief Access the underlying allocator (raw pointer).
     *
     * Used by zero-overhead APIs such as PoolMakeUnique to build stateless
     * deleters. The pointer stays valid as long as this AlloterWrap is
     * alive.
     */
    IAlloter* GetAlloter() const noexcept { return alloter_.get(); }

private:
    std::shared_ptr<IAlloter> alloter_;
};

template<typename T, typename... Args>
T* AlloterWrap::PoolNew(Args&&... args) {
    uint32_t sz = sizeof(T);
    
    void* data = alloter_->MallocAlign(sz);
    if (!data) {
        return nullptr;
    }

    T* res = new(data) T(std::forward<Args>(args)...);
    return res;
}

template<typename T, typename... Args>
PoolUniquePtr<T> AlloterWrap::PoolMakeUnique(Args&&... args) {
    T* p = PoolNew<T>(std::forward<Args>(args)...);
    // Stateless deleter: only a raw IAlloter*. No atomic refcount, no capture.
    return PoolUniquePtr<T>(p, PoolDeleter<T>(alloter_.get()));
}

template<typename T, typename... Args>
std::shared_ptr<T> AlloterWrap::PoolMakeShared(Args&&... args) {
    // std::allocate_shared lets the STL place BOTH the control block and the
    // T object in a single allocation served by our pool. This is the
    // pool-aware analogue of std::make_shared.
    return std::allocate_shared<T>(PoolStdAllocator<T>(alloter_.get()),
                                   std::forward<Args>(args)...);
}

template<typename T, typename... Args >
std::shared_ptr<T> AlloterWrap::PoolNewSharePtr(Args&&... args) {
    // Implemented in terms of PoolMakeShared so the legacy API also benefits
    // from the pooled control block. Kept around only for source compatibility
    // with callers still using the old name; new code should call
    // PoolMakeShared() (or, better, PoolMakeUnique() / PoolNew()).
    return PoolMakeShared<T>(std::forward<Args>(args)...);
}

template<typename T>
void AlloterWrap::PoolDelete(T* c) {
    if (!c) {
        return;
    }

    c->~T();

    uint32_t len = sizeof(T);
    void* data = (void*)c;
    alloter_->Free(data, len);
}
    
template<typename T>
T* AlloterWrap::PoolMalloc(uint32_t sz) {  
    return (T*)alloter_->MallocAlign(sz);
}

template<typename T>
std::shared_ptr<T> AlloterWrap::PoolMallocSharePtr(uint32_t size) {
    T* ret = PoolMalloc<T>(size);
    // Capture only the raw IAlloter*: no atomic refcount on the captured
    // shared_ptr<IAlloter>, smaller deleter footprint. Lifetime contract is
    // the same as PoolUniquePtr (alloter must outlive the shared_ptr).
    IAlloter* raw = alloter_.get();
    return std::shared_ptr<T>(ret, [raw, size](T* c) {
        if (!c) return;
        void* data = (void*)c;
        raw->Free(data, size);
    });
}
    
template<typename T>
void AlloterWrap::PoolFree(T* m, uint32_t len) {
    if (!m) {
        return;
    }
    void* data = (void*)m;
    alloter_->Free(data, len);
}

}
}

#endif 