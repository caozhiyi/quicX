#ifndef COMMON_ALLOTER_IF_ALLOTER
#define COMMON_ALLOTER_IF_ALLOTER

#include <memory>
#include <cstdint>

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
 * @brief Wrapper for pool-based object and memory allocation
 *
 * Provides convenient methods for allocating objects and memory from a pool.
 */
class AlloterWrap {
public:
    AlloterWrap(std::shared_ptr<IAlloter> a) : alloter_(a) {}
    ~AlloterWrap() {}

    /**
     * @brief Allocate and construct an object from the pool
     *
     * @tparam T Object type
     * @tparam Args Constructor argument types
     * @param args Constructor arguments
     * @return Pointer to constructed object, or nullptr on failure
     */
    template<typename T, typename... Args >
    T* PoolNew(Args&&... args);

    /**
     * @brief Allocate and construct an object, returning a shared_ptr
     *
     * @tparam T Object type
     * @tparam Args Constructor argument types
     * @param args Constructor arguments
     * @return shared_ptr to constructed object
     */
    template<typename T, typename... Args >
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
     * @tparam T Element type
     * @param size Size in bytes
     * @return shared_ptr to allocated memory
     */
    template<typename T>
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

template<typename T, typename... Args >
std::shared_ptr<T> AlloterWrap::PoolNewSharePtr(Args&&... args) {
    T* ret = PoolNew<T>(std::forward<Args>(args)...);
    return std::shared_ptr<T>(ret, [this](T* c) { PoolDelete(c); });
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
    return std::shared_ptr<T>(ret, [this, size](T* c) { PoolFree(c, size); });
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