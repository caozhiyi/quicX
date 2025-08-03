#ifndef COMMON_STRUCTURE_THREAD_SAFE_BLOCK_QUEUE
#define COMMON_STRUCTURE_THREAD_SAFE_BLOCK_QUEUE

#include <queue>
#include <mutex>
#include <chrono>
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
        empty_notify_.wait(lock, [this]() {return !this->queue_.empty(); });

        auto ret = std::move(queue_.front());
        queue_.pop();
 
        return ret;
    }

    // Try to pop without blocking
    bool TryPop(T& element) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        element = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    // Try to pop with timeout
    bool TryPop(T& element, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!empty_notify_.wait_for(lock, timeout, [this]() {return !this->queue_.empty(); })) {
            return false;
        }
        element = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    void Clear() {
        std::unique_lock<std::mutex> lock(mutex_);
        std::queue<T> empty;
        queue_.swap(empty);
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