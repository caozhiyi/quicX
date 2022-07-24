#ifndef COMMON_UTIL_C_SMART_PTR
#define COMMON_UTIL_C_SMART_PTR

#include <memory>

namespace quicx {

/**
 * This is a helper that wraps C style API objects that need to be deleted with a smart pointer.
 */
template <class T, void (*deleter)(T*)>
class CSmartPtr:
    public std::unique_ptr<T, void (*)(T*)> {
public:
    CSmartPtr(): std::unique_ptr<T, void (*)(T*)>(nullptr, deleter) {}
    CSmartPtr(T* object) : std::unique_ptr<T, void (*)(T*)>(object, deleter) {}
};

}

#endif