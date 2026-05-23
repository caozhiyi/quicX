#ifndef COMMON_ALLOTER_IF_ALLOTER
#define COMMON_ALLOTER_IF_ALLOTER

#include <memory>
#include <cstdint>
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
 * @brief Wrapper for pool-based object and memory allocation
 *
 * Provides convenient methods for allocating objects and memory from a pool.
 *
 * Preferred API for hot paths (in descending order of performance):
 *   1. PoolNew<T>(...) + PoolDelete<T>(p)   - raw pointer, manual lifetime.
 *   2. PoolMakeUnique<T>(...)               - RAII via unique_ptr, same perf.
 *   3. PoolNewSharePtr<T>(...)              - DEPRECATED, see its doc.
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
     * @brief Allocate and construct an object, returning a shared_ptr.
     *
     * @deprecated This path is slower than raw new/delete because:
     *   (1) the shared_ptr control block is allocated by the default
     *       allocator (NOT the pool), completely bypassing pooling;
     *   (2) the deleter captures a std::shared_ptr<IAlloter>, adding an
     *       atomic inc/dec on every shared_ptr copy/destruction;
     *   (3) the type-erased deleter inflates the control block (~48B).
     *
     * Use PoolMakeUnique<T>() (zero overhead, RAII) or PoolNew<T>() + manual
     * PoolDelete<T>() on hot paths. Keep this API only where shared
     * ownership across subsystems is genuinely required and performance
     * is not critical.
     */
    template<typename T, typename... Args >
    [[deprecated("Slow on hot paths: shared_ptr control block is not pooled. "
                 "Use PoolMakeUnique<T>() or PoolNew<T>() + PoolDelete<T>() instead.")]]
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
     * @deprecated Same overhead concerns as PoolNewSharePtr. Prefer
     * PoolMalloc<T>() + PoolFree<T>() when possible.
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

template<typename T, typename... Args >
std::shared_ptr<T> AlloterWrap::PoolNewSharePtr(Args&&... args) {
    T* ret = PoolNew<T>(std::forward<Args>(args)...);
    auto alloter = alloter_;
    return std::shared_ptr<T>(ret, [alloter](T* c) {
        if (!c) return;
        c->~T();
        uint32_t len = sizeof(T);
        void* data = (void*)c;
        alloter->Free(data, len);
    });
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
    auto alloter = alloter_;
    return std::shared_ptr<T>(ret, [alloter, size](T* c) {
        if (!c) return;
        void* data = (void*)c;
        alloter->Free(data, size);
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