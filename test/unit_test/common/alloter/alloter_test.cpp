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
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
        auto at = IAlloter.PoolNewSharePtr<AlloterTestClass>(100);
        ASSERT_EQ(100, at->data_);
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif
    }
    ASSERT_EQ(0, kAlloterTestValue);
}

TEST(alloter_utest, pool_make_unique) {
    AlloterWrap IAlloter(std::shared_ptr<IAlloter>(new PoolAlloter()));
    {
        auto at = IAlloter.PoolMakeUnique<AlloterTestClass>(100);
        ASSERT_EQ(100, at->data_);
        static_assert(sizeof(at) == sizeof(void*) * 2,
                      "PoolUniquePtr must hold exactly {T*, IAlloter*}");
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
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
        auto data = IAlloter.PoolMallocSharePtr<char>(100);
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif
    }
}

}
}
}