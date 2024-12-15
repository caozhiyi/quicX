// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_STRUCTURE_LINKED_LIST
#define COMMON_STRUCTURE_LINKED_LIST

#include <memory>
#include <cstdint>
#include "common/structure/linked_list_solt.h"

namespace quicx {
namespace common {

template<typename T>
class LinkedList {
public:
    LinkedList(): size_(0), head_(nullptr), tail_(nullptr) {}
    ~LinkedList() {}

    uint32_t Size() { return size_; }

    std::shared_ptr<T> GetHead() { return head_; }
    std::shared_ptr<T> GetTail() { return tail_; }

    void Clear() {
        size_ = 0;

        head_.reset();
        tail_.reset();
    }

    void PushBack(std::shared_ptr<T> v) {
        if (!v) {
            return;
        }
    
        if (!tail_) {
            tail_ = v;
            head_ = v;

        } else {
            tail_->SetNext(v);
            tail_ = v;
        }
        size_++;
    }

    std::shared_ptr<T> PopFront() {
        if (!head_) {
            return nullptr;
        }

        auto ret = head_;
        head_ = head_->GetNext();
        if (!head_) {
            tail_.reset();
        }
        
        size_--;
        return ret;
    }

private:
    uint32_t size_;
    std::shared_ptr<T> head_;
    std::shared_ptr<T> tail_;
};

}
}

#endif