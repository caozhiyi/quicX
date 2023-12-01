#include <mutex>
#include <thread>
#include <gtest/gtest.h>
#include "common/alloter/pool_alloter.h"

namespace quicx {
namespace common {
namespace {

uint32_t __alloter_test_value = 0;
class AlloterTestClass {
public:
    AlloterTestClass(uint64_t v) : _data(v) {
        __alloter_test_value++;
    }
    ~AlloterTestClass() {
         __alloter_test_value--;
    }

    uint64_t _data;
};

TEST(alloter_utest, warp1) {
    AlloterWrap IAlloter(std::shared_ptr<IAlloter>(new PoolAlloter()));
    AlloterTestClass* at = IAlloter.PoolNew<AlloterTestClass>(100);
    ASSERT_EQ(100, at->_data);
    IAlloter.PoolDelete<AlloterTestClass>(at);
    ASSERT_EQ(0, __alloter_test_value);
}


TEST(alloter_utest, warp2) {
    AlloterWrap IAlloter(std::shared_ptr<IAlloter>(new PoolAlloter()));
    {
        auto at = IAlloter.PoolNewSharePtr<AlloterTestClass>(100);
        ASSERT_EQ(100, at->_data);
    }
    ASSERT_EQ(0, __alloter_test_value);
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