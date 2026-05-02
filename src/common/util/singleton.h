#ifndef COMMON_UTIL_SINGLETON
#define COMMON_UTIL_SINGLETON

namespace quicx {
namespace common {

template<typename T>
class Singleton {
public:
    static T& Instance() {
        static T instance;
        return instance;
    }

protected:
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;
    Singleton() {}
    virtual ~Singleton() {}
};

}
}

#endif