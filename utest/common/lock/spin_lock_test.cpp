#include <mutex>
#include <thread>
#include <gtest/gtest.h>
#include "../time_consuming.h"
#include "common/lock/spin_lock.h"

/*TEST(spin_lock_utest, spin_lock_1) {
    int flag = 0;
    quicx::SpinLock lock;
    std::thread th1([&lock, &flag](){
            for (size_t i = 0; i < 200000; i++) {
                quicx::UniqueLock l(lock);
                flag--;
            }
        }
    );
    std::thread th2([&lock, &flag](){
            for (size_t i = 0; i < 200000; i++) {
                quicx::UniqueLock l(lock);
                flag++;
            }
        }
    );

    th1.join();
    th2.join();

    EXPECT_EQ(flag, 0);
}

TEST(spin_lock_utest, spin_lock_2) {
    int flag = 0;
    quicx::SpinLock lock;
    std::mutex mu;

    {
        quicx::TimeConsuming tc("spin_lock_1");
        for (size_t i = 0; i < 2000000; i++) {
            lock.Lock();
            lock.Unlock();
        }
    }
    
    {
        quicx::TimeConsuming tc("mutex_lock_1");
        for (size_t i = 0; i < 2000000; i++) {
            mu.lock();
            mu.unlock();
        }
    }
}


TEST(spin_lock_utest, spin_lock_3) {
    int flag = 0;
    quicx::SpinLock lock;
    std::mutex mu;

    {
        std::thread th1([&lock](){
            for (size_t i = 0; i < 2000000; i++) {
                lock.Lock();
                lock.Unlock();
            }
        });
        std::thread th2([&lock](){
            for (size_t i = 0; i < 2000000; i++) {
                lock.Lock();
                lock.Unlock();
            }
        });
        quicx::TimeConsuming tc("spin_lock_2");
        for (size_t i = 0; i < 2000000; i++) {
            lock.Lock();
            lock.Unlock();
        }
        th1.join();
        th2.join();
    }
    
    {
        std::thread th1([&mu](){
            for (size_t i = 0; i < 2000000; i++) {
                mu.lock();
                mu.unlock();
            }
        });
        std::thread th2([&mu](){
            for (size_t i = 0; i < 2000000; i++) {
                mu.lock();
                mu.unlock();
            }
        });
        quicx::TimeConsuming tc("mutex_lock_2");
        for (size_t i = 0; i < 2000000; i++) {
            mu.lock();
            mu.unlock();
        }
        th1.join();
        th2.join();
    }
}
*/