#ifndef COMMON_STRUCTURE_THREAD_SAFE_QUEUE
#define COMMON_STRUCTURE_THREAD_SAFE_QUEUE

#include <mutex>
#include <queue>

namespace quicx {
namespace common {

template<typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() {}
    ~ThreadSafeQueue() {}

    void Push(const T& element) {
        std::unique_lock<std::mutex> lock(mutex_);
        queue_.push(element);
    }

    void Emplace(T&& element) {
        std::unique_lock<std::mutex> lock(mutex_);
        queue_.emplace(std::move(element));
    }

    bool Pop(T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        value = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    void Clear() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!queue_.empty()) {
            queue_.pop();
        }
    }

    size_t Size() {
        std::unique_lock<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool Empty() {
        std::unique_lock<std::mutex> lock(mutex_);
        return queue_.empty();
    }

private:
    std::queue<T>        queue_;
    std::mutex           mutex_;
};

}
}

#endif