#ifndef COMMON_LOCK_SPIN_LOCK
#define COMMON_LOCK_SPIN_LOCK

#include <atomic>
#include <mutex>

namespace quicx {
namespace common {

// spain lock
class SpinLock {
public:
    SpinLock();
    ~SpinLock();

    // try to lock
    // return soon
    bool TryLock();
    // block to get lock 
    bool Lock();

    // release lock
    void Unlock();
private:
    std::atomic_flag lock_;
};

class UniqueLock {
public:
    UniqueLock(SpinLock& l):lock_(&l){
        lock_->Lock();
    }

    ~UniqueLock() {
        lock_->Unlock();
    }

private:
    SpinLock* lock_;
};

}
}

#endif