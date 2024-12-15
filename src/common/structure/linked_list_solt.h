#ifndef COMMON_STRUCTURE_LINKED_LIST_SOLT
#define COMMON_STRUCTURE_LINKED_LIST_SOLT

#include <memory>

namespace quicx {
namespace common {

template<typename T>
class LinkedListSolt {
public:
    LinkedListSolt(): next_(nullptr) {}
    virtual ~LinkedListSolt() {}

    void SetNext(std::shared_ptr<T> v) { next_ = v; }
    std::shared_ptr<T> GetNext() { return next_; }

protected:
    std::shared_ptr<T> next_;
};

}
}

#endif