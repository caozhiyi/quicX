#ifndef COMMON_STRUCTURE_QUEUE_INTERFACE
#define COMMON_STRUCTURE_QUEUE_INTERFACE

#include <memory>

namespace quicx {

class QueueSolt;

class Queue {
public:
Queue() {}
virtual ~Queue() {}

virtual std::shared_ptr<QueueSolt> Front() = 0;
virtual std::shared_ptr<QueueSolt> Back() = 0;

virtual bool IsEmpty() = 0;
virtual size_t Size() = 0;

virtual void Push(std::shared_ptr<QueueSolt> s) = 0;
virtual void Pop() = 0;

virtual void Swap(std::shared_ptr<Queue>& q) = 0;

};

}

#endif