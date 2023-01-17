#ifndef COMMON_STRUCTURE_LINKED_LIST_SOLT
#define COMMON_STRUCTURE_LINKED_LIST_SOLT

#include <memory>

namespace quicx {

template<typename T>
class LinkedListSolt {
public:
    LinkedListSolt(): _next(nullptr) {}
    virtual ~LinkedListSolt() {}

    void SetNext(std::shared_ptr<T> v) { _next = v; }
    std::shared_ptr<T> GetNext() { return _next; }

protected:
    std::shared_ptr<T> _next;
};

}

#endif