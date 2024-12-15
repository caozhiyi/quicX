#ifndef COMMON_THREAD_THREAD_WITH_QUEUE
#define COMMON_THREAD_THREAD_WITH_QUEUE

#include "thread.h"
#include "common/structure/thread_safe_block_queue.h"

namespace quicx {
namespace common {
    
template<typename T>
class ThreadWithQueue:
    public Thread {
public:
    ThreadWithQueue() {}
    virtual ~ThreadWithQueue() {}

    uint32_t GetQueueSize() {
        return queue_.Size();
    }

    void Push(const T& t) {
        queue_.Push(t);
    }

    T Pop() {
        return std::move(queue_.Pop());
    }

    //TO DO
    virtual void Run() = 0;

protected:
    ThreadWithQueue(const ThreadWithQueue&) = delete;
    ThreadWithQueue& operator=(const ThreadWithQueue&) = delete;

protected:
    ThreadSafeBlockQueue<T> queue_;
};

}
}

#endif