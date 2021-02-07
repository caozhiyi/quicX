#include "queue_thread_local.h"

namespace quicx {

thread_local std::shared_ptr<QueueThreadLocal::thread_queue>   \
    QueueThreadLocal::_queue_ptr(new QueueThreadLocal::thread_queue);

QueueThreadLocal::QueueThreadLocal() {
    
}

QueueThreadLocal::~QueueThreadLocal() {
   
}

std::shared_ptr<QueueSolt> QueueThreadLocal::Front() {
    UniqueLock lock(_sp_lock);
    return _queue_ptr->front();
}

std::shared_ptr<QueueSolt> QueueThreadLocal::Back() {
    UniqueLock lock(_sp_lock);
    return _queue_ptr->back();
}

bool QueueThreadLocal::IsEmpty() {
    UniqueLock lock(_sp_lock);
    return _queue_ptr->empty();
}

size_t QueueThreadLocal::Size() {
    UniqueLock lock(_sp_lock);
    return _queue_ptr->size();
}

void QueueThreadLocal::Push(std::shared_ptr<QueueSolt> s) {
    UniqueLock lock(_sp_lock);
    _queue_ptr->push(s);
}

void QueueThreadLocal::Pop() {
    UniqueLock lock(_sp_lock);
    _queue_ptr->pop();
}

void QueueThreadLocal::Swap(std::shared_ptr<Queue>& q) {

}

}