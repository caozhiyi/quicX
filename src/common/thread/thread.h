#ifndef COMMON_THREAD_THREAD
#define COMMON_THREAD_THREAD

#include <thread>     // about thread
#include <atomic>     // for atomic_bool
#include <memory>     // for shared_ptr
#include <functional> // for bind

namespace quicx {
namespace common {

class Thread {
public:
    Thread(): stop_(true) {}
    virtual ~Thread() {
        Stop();
        if (pthread_ && pthread_->joinable()) {
            pthread_->join();
        }
    }

    //base option
    virtual void Start() {
        stop_ = false;
        if (!pthread_) {
            pthread_ = std::make_shared<std::thread>(std::bind(&Thread::Run, this));
        }
    }

    virtual void Stop() {
        stop_ = true;
    }

    virtual void Join() {
        // joinable() guard: Join() may be called multiple times across nested
        // teardown paths (e.g. Http3 Client::~Client calls quic_->Join() and
        // then ~QuicClient also calls master_->Join()). Without this guard
        // the second join() throws std::system_error("Invalid argument").
        if (pthread_ && pthread_->joinable()) {
            pthread_->join();
        }
    }
    //TO DO
    virtual void Run() = 0;

    virtual bool IsStop() {
        return stop_;
    }

protected:
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

protected:
    std::atomic_bool stop_;
    std::shared_ptr<std::thread> pthread_;
};

}
}

#endif