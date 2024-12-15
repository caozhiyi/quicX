#include "spin_lock.h"

namespace quicx {
namespace common {

static bool flag = true;

SpinLock::SpinLock() {
}

SpinLock::~SpinLock() {

}

bool SpinLock::TryLock() {
    return !lock_.test_and_set(std::memory_order_acquire);
}

bool SpinLock::Lock() {
    while (lock_.test_and_set(std::memory_order_acquire)) { 

    }
    return true;
}

void SpinLock::Unlock() {
    lock_.clear(std::memory_order_release);
}

}
}