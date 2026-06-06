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

TEST(alloter_utest, pool_make_shared_basic) {
    AlloterWrap wrap(std::shared_ptr<IAlloter>(new PoolAlloter()));
    {
        auto sp = wrap.PoolMakeShared<AlloterTestClass>(123);
        ASSERT_EQ(123u, sp->data_);
        ASSERT_EQ(1u, kAlloterTestValue);
        // Sanity: shared_ptr is the regular two-pointer fat pointer; the
        // pooling happens inside the control block, not in the shared_ptr
        // itself.
        ASSERT_EQ(1L, sp.use_count());
    }
    ASSERT_EQ(0u, kAlloterTestValue);
}

TEST(alloter_utest, pool_make_shared_copy_semantics) {
    AlloterWrap wrap(std::shared_ptr<IAlloter>(new PoolAlloter()));
    {
        auto sp1 = wrap.PoolMakeShared<AlloterTestClass>(7);
        ASSERT_EQ(1L, sp1.use_count());
        {
            auto sp2 = sp1; // copy bumps refcount in the (pooled) control block
            ASSERT_EQ(2L, sp1.use_count());
            ASSERT_EQ(2L, sp2.use_count());
            ASSERT_EQ(1u, kAlloterTestValue);
        }
        // sp2 destroyed but sp1 still alive: object must NOT be freed yet.
        ASSERT_EQ(1u, kAlloterTestValue);
        ASSERT_EQ(1L, sp1.use_count());
    }
    // both gone -> control block destroyed and (eventually) deallocated.
    ASSERT_EQ(0u, kAlloterTestValue);
}

TEST(alloter_utest, pool_make_shared_weak_ptr_outlives_object) {
    AlloterWrap wrap(std::shared_ptr<IAlloter>(new PoolAlloter()));
    std::weak_ptr<AlloterTestClass> wp;
    {
        auto sp = wrap.PoolMakeShared<AlloterTestClass>(42);
        wp = sp;
        ASSERT_FALSE(wp.expired());
        ASSERT_EQ(42u, wp.lock()->data_);
    }
    // Object is gone, but the control block (also pool-allocated) must still
    // be valid for weak_ptr to query expired() / lock() safely.
    ASSERT_TRUE(wp.expired());
    ASSERT_EQ(nullptr, wp.lock());
    ASSERT_EQ(0u, kAlloterTestValue);
    // wp itself goes out of scope here: control block deallocates back to
    // the pool. The pool, owned by `wrap`, outlives this scope so the
    // contract holds.
}

TEST(alloter_utest, pool_std_allocator_equality) {
    auto alloter_a = std::shared_ptr<IAlloter>(new PoolAlloter());
    auto alloter_b = std::shared_ptr<IAlloter>(new PoolAlloter());
    PoolStdAllocator<int> a1(alloter_a.get());
    PoolStdAllocator<int> a2(alloter_a.get());
    PoolStdAllocator<int> b1(alloter_b.get());
    // Equality is defined by the underlying IAlloter*, which is what
    // std::allocator_traits and STL containers rely on for safe rebinding.
    ASSERT_TRUE(a1 == a2);
    ASSERT_FALSE(a1 == b1);
    ASSERT_TRUE(a1 != b1);
    // Rebound allocator must compare equal to the original (same pool).
    PoolStdAllocator<char> a1_rebound(a1);
    ASSERT_EQ(a1.GetAlloter(), a1_rebound.GetAlloter());
}

TEST(alloter_utest, pool_new_share_ptr_uses_pooled_control_block) {
    // PoolNewSharePtr is now implemented in terms of PoolMakeShared, so it
    // should behave identically (same dtor counting, same use_count semantics)
    // even though it remains [[deprecated]] for clarity at call sites.
    AlloterWrap wrap(std::shared_ptr<IAlloter>(new PoolAlloter()));
    {
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
        auto sp = wrap.PoolNewSharePtr<AlloterTestClass>(99);
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif
        ASSERT_EQ(99u, sp->data_);
        ASSERT_EQ(1u, kAlloterTestValue);
    }
    ASSERT_EQ(0u, kAlloterTestValue);
}

}
}
}