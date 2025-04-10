// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_ALLOTER_IF_ALLOTER
#define COMMON_ALLOTER_IF_ALLOTER

#include <memory>
#include <cstdint>

namespace quicx {
namespace common {

static const uint16_t kAlign = sizeof(unsigned long);

class IAlloter {
public:
    IAlloter() {}
    virtual ~IAlloter() {}

    virtual void* Malloc(uint32_t size) = 0;
    virtual void* MallocAlign(uint32_t size) = 0;
    virtual void* MallocZero(uint32_t size) = 0;

    virtual void Free(void* &data, uint32_t len = 0) = 0;

protected:
    uint32_t Align(uint32_t size) {
        return ((size + kAlign - 1) & ~(kAlign - 1));
    }
};

class AlloterWrap {
public:
    AlloterWrap(std::shared_ptr<IAlloter> a) : alloter_(a) {}
    ~AlloterWrap() {}

    //for object. invocation of constructors and destructors
    template<typename T, typename... Args >
    T* PoolNew(Args&&... args);
    template<typename T, typename... Args >
    std::shared_ptr<T> PoolNewSharePtr(Args&&... args);

    template<typename T>
    void PoolDelete(T* c);

    //for continuous memory
    template<typename T>
    T* PoolMalloc(uint32_t size);
    template<typename T>
    std::shared_ptr<T> PoolMallocSharePtr(uint32_t size);

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