#include <mutex>
#include <thread>
#include <gtest/gtest.h>
#include "common/alloter/pool_alloter.h"

namespace quicx {
namespace common {
namespace {

static uint32_t kAlloterTestValue = 0;
class AlloterTestClass {
public:
    AlloterTestClass(uint64_t v) : data_(v) {
        kAlloterTestValue++;
    }
    ~AlloterTestClass() {
         kAlloterTestValue--;
    }

    uint64_t data_;
};

TEST(alloter_utest, warp1) {
    AlloterWrap IAlloter(std::shared_ptr<IAlloter>(new PoolAlloter()));
    AlloterTestClass* at = IAlloter.PoolNew<AlloterTestClass>(100);
    ASSERT_EQ(100, at->data_);
    IAlloter.PoolDelete<AlloterTestClass>(at);
    ASSERT_EQ(0, kAlloterTestValue);
}


TEST(alloter_utest, warp2) {
    AlloterWrap IAlloter(std::shared_ptr<IAlloter>(new PoolAlloter()));
    {
        auto at = IAlloter.PoolNewSharePtr<AlloterTestClass>(100);
        ASSERT_EQ(100, at->data_);
    }
    ASSERT_EQ(0, kAlloterTestValue);
}

TEST(alloter_utest, warp3) {
    AlloterWrap IAlloter(std::shared_ptr<IAlloter>(new PoolAlloter()));
    auto data = IAlloter.PoolMalloc<char>(100);
    IAlloter.PoolFree<char>(data, 100);
}


TEST(alloter_utest, warp4) {
    AlloterWrap IAlloter(std::shared_ptr<IAlloter>(new PoolAlloter()));
    {
        auto data = IAlloter.PoolMallocSharePtr<char>(100);
    }
}

}
}
}