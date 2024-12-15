#ifndef COMMON_STRUCTURE_QUEUE_THREAD_LOCAL
#define COMMON_STRUCTURE_QUEUE_THREAD_LOCAL

#include <queue>
#include "common/lock/spin_lock.h"
#include "common/structure/if_queue.h"

namespace quicx {
namespace common {

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

SpinLock sp_lock_;
typedef std::queue<std::shared_ptr<QueueSolt>> thread_queue;
static thread_local std::shared_ptr<thread_queue> queue_ptr_;

};

}
}

#endif