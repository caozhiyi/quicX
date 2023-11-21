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
    std::atomic_flag _lock;
};

class UniqueLock {
public:
    UniqueLock(SpinLock& l):_lock(&l){
        _lock->Lock();
    }

    ~UniqueLock() {
        _lock->Unlock();
    }

private:
    SpinLock* _lock;
};

}
}

#endif