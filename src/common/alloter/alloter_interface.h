#ifndef COMMON_ALLOTER_INTERFACE
#define COMMON_ALLOTER_INTERFACE

#include <memory>
#include <cstdint>

namespace quicx {

static const int __align = sizeof(unsigned long);

class Alloter {
public:
    Alloter() {}
    virtual ~Alloter() {}

    virtual void* Malloc(uint32_t size) = 0;
    virtual void* MallocAlign(uint32_t size) = 0;
    virtual void* MallocZero(uint32_t size) = 0;

    virtual void Free(void* &data, uint32_t len) = 0;

protected:
    uint32_t Align(uint32_t size) {
        return ((size + __align - 1) & ~(__align - 1));
    }
};

class AlloterWrap {
public:
    AlloterWrap(std::shared_ptr<Alloter> a) : _alloter(a) {}
    ~AlloterWrap() {}

    //for object. invocation of constructors and destructors
    template<typename T, typename... Args >
    T* PoolNew(Args&&... args);
    template<typename T>
    void PoolDelete(T* &c);
    
    //for continuous memory
    template<typename T>
    T* PoolMalloc(uint32_t size);
    template<typename T>
    void PoolFree(T* &m, uint32_t len);

private:
    std::shared_ptr<Alloter> _alloter;
};

template<typename T, typename... Args>
T* AlloterWrap::PoolNew(Args&&... args) {
    uint32_t sz = sizeof(T);
    
    void* data = _alloter->MallocAlign(sz);
    if (!data) {
        return nullptr;
    }

    T* res = new(data) T(std::forward<Args>(args)...);
    return res;
}
    
template<typename T>
void AlloterWrap::PoolDelete(T* &c) {
    if (!c) {
        return;
    }
    
    uint32_t sz = sizeof(T);
    _alloter->Free(c, sz);
}
    
template<typename T>
T* AlloterWrap::PoolMalloc(uint32_t sz) {  
    return (T*)_alloter->MallocAlign(sz);
}
    
template<typename T>
void AlloterWrap::PoolFree(T* &m, uint32_t len) {
    if (!m) {
        return;
    }
    _alloter->Free(m, len);   
}

}

#endif 