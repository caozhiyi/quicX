#include "spin_lock.h"

namespace quicx {
namespace common {

static bool flag = true;

SpinLock::SpinLock() {
}

SpinLock::~SpinLock() {

}

bool SpinLock::TryLock() {
    return !_lock.test_and_set(std::memory_order_acquire);
}

bool SpinLock::Lock() {
    while (_lock.test_and_set(std::memory_order_acquire)) { 

    }
    return true;
}

void SpinLock::Unlock() {
    _lock.clear(std::memory_order_release);
}

}
}