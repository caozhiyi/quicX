#ifndef COMMON_QUEUE_QUEUE_THREAD_LOCAL
#define COMMON_QUEUE_QUEUE_THREAD_LOCAL

#include <queue>
#include "lock/spin_lock.h"
#include "queue_interface.h"

namespace quicx {

class QueueSolt;
class QueueThreadLocal: public Queue{
public:
QueueThreadLocal();
~QueueThreadLocal();
std::shared_ptr<QueueSolt> Front();
std::shared_ptr<QueueSolt> Back();

bool IsEmpty();
size_t Size();

void Push(std::shared_ptr<QueueSolt> s);
void Pop();

void Swap(std::shared_ptr<Queue>& q);

private:

SpinLock _sp_lock;
typedef std::queue<std::shared_ptr<QueueSolt>> thread_queue;
static thread_local std::shared_ptr<thread_queue> _queue_ptr;

};

}


#endif