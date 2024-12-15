#ifndef COMMON_STRUCTURE_THREAD_SAFE_BLOCK_QUEUE
#define COMMON_STRUCTURE_THREAD_SAFE_BLOCK_QUEUE

#include <queue>
#include <mutex>
#include <condition_variable>

namespace quicx {
namespace common {

template<typename T>
class ThreadSafeBlockQueue {
public:
    ThreadSafeBlockQueue() {}
    ~ThreadSafeBlockQueue() {}

    void Push(const T& element) {
        std::unique_lock<std::mutex> lock(mutex_);
        queue_.push(element);
        empty_notify_.notify_all();
    }

    T Pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        empty_notify_.wait(mutex_, [this]() {return !this->queue_.empty(); });

        auto ret = std::move(queue_.front());
        queue_.pop();
 
        return std::move(ret);
    }

    void Clear() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!queue_.empty()) {
            queue_.pop();
        }
    }

    uint32_t Size() {
        std::unique_lock<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool Empty() {
        std::unique_lock<std::mutex> lock(mutex_);
        return queue_.empty();
    }

private:
    std::mutex    mutex_;
    std::queue<T> queue_;
    std::condition_variable_any empty_notify_;
};

}
}

#endif