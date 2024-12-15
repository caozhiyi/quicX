#include "common/structure/queue_thread_local.h"

namespace quicx {
namespace common {

thread_local std::shared_ptr<QueueThreadLocal::thread_queue>   \
    QueueThreadLocal::queue_ptr_(new QueueThreadLocal::thread_queue);

QueueThreadLocal::QueueThreadLocal() {

}

QueueThreadLocal::~QueueThreadLocal() {
   
}

std::shared_ptr<QueueSolt> QueueThreadLocal::Front() {
    UniqueLock lock(sp_lock_);
    return queue_ptr_->front();
}

std::shared_ptr<QueueSolt> QueueThreadLocal::Back() {
    UniqueLock lock(sp_lock_);
    return queue_ptr_->back();
}

bool QueueThreadLocal::IsEmpty() {
    UniqueLock lock(sp_lock_);
    return queue_ptr_->empty();
}

size_t QueueThreadLocal::Size() {
    UniqueLock lock(sp_lock_);
    return queue_ptr_->size();
}

void QueueThreadLocal::Push(std::shared_ptr<QueueSolt> s) {
    UniqueLock lock(sp_lock_);
    queue_ptr_->push(s);
}

void QueueThreadLocal::Pop() {
    UniqueLock lock(sp_lock_);
    queue_ptr_->pop();
}

void QueueThreadLocal::Swap(std::shared_ptr<Queue>& q) {

}

}
}