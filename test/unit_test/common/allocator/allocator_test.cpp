#include <gtest/gtest.h>

#include "common/allocator/pool_allocator.h"

namespace quicx {
namespace common {
namespace {

static uint32_t kallocatorTestValue = 0;
class allocatorTestClass {
public:
    allocatorTestClass(uint64_t v):
        data_(v) {
        kallocatorTestValue++;
    }
    ~allocatorTestClass() { kallocatorTestValue--; }

    uint64_t data_;
};

TEST(allocatorTest, warp1) {
    allocatorWrap IAllocator(std::shared_ptr<IAllocator>(new PoolAllocator()));
    allocatorTestClass* at = IAllocator.PoolNew<allocatorTestClass>(100);
    ASSERT_EQ(100, at->data_);
    IAllocator.PoolDelete<allocatorTestClass>(at);
    ASSERT_EQ(0, kallocatorTestValue);
}

TEST(allocatorTest, warp2) {
    allocatorWrap IAllocator(std::shared_ptr<IAllocator>(new PoolAllocator()));
    {
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
        auto at = IAllocator.PoolNewSharePtr<allocatorTestClass>(100);
        ASSERT_EQ(100, at->data_);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
    }
    ASSERT_EQ(0, kallocatorTestValue);
}

TEST(allocatorTest, pool_make_unique) {
    allocatorWrap IAllocator(std::shared_ptr<IAllocator>(new PoolAllocator()));
    {
        auto at = IAllocator.PoolMakeUnique<allocatorTestClass>(100);
        ASSERT_EQ(100, at->data_);
        static_assert(sizeof(at) == sizeof(void*) * 2, "PoolUniquePtr must hold exactly {T*, IAllocator*}");
    }
    ASSERT_EQ(0, kallocatorTestValue);
}

TEST(allocatorTest, warp3) {
    allocatorWrap IAllocator(std::shared_ptr<IAllocator>(new PoolAllocator()));
    auto data = IAllocator.PoolMalloc<char>(100);
    IAllocator.PoolFree<char>(data, 100);
}

TEST(allocatorTest, warp4) {
    allocatorWrap IAllocator(std::shared_ptr<IAllocator>(new PoolAllocator()));
    {
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
        auto data = IAllocator.PoolMallocSharePtr<char>(100);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
    }
}

TEST(allocatorTest, pool_make_shared_basic) {
    allocatorWrap wrap(std::shared_ptr<IAllocator>(new PoolAllocator()));
    {
        auto sp = wrap.PoolMakeShared<allocatorTestClass>(123);
        ASSERT_EQ(123u, sp->data_);
        ASSERT_EQ(1u, kallocatorTestValue);
        // Sanity: shared_ptr is the regular two-pointer fat pointer; the
        // pooling happens inside the control block, not in the shared_ptr
        // itself.
        ASSERT_EQ(1L, sp.use_count());
    }
    ASSERT_EQ(0u, kallocatorTestValue);
}

TEST(allocatorTest, pool_make_shared_copy_semantics) {
    allocatorWrap wrap(std::shared_ptr<IAllocator>(new PoolAllocator()));
    {
        auto sp1 = wrap.PoolMakeShared<allocatorTestClass>(7);
        ASSERT_EQ(1L, sp1.use_count());
        {
            auto sp2 = sp1;  // copy bumps refcount in the (pooled) control block
            ASSERT_EQ(2L, sp1.use_count());
            ASSERT_EQ(2L, sp2.use_count());
            ASSERT_EQ(1u, kallocatorTestValue);
        }
        // sp2 destroyed but sp1 still alive: object must NOT be freed yet.
        ASSERT_EQ(1u, kallocatorTestValue);
        ASSERT_EQ(1L, sp1.use_count());
    }
    // both gone -> control block destroyed and (eventually) deallocated.
    ASSERT_EQ(0u, kallocatorTestValue);
}

TEST(allocatorTest, pool_make_shared_weak_ptr_outlives_object) {
    allocatorWrap wrap(std::shared_ptr<IAllocator>(new PoolAllocator()));
    std::weak_ptr<allocatorTestClass> wp;
    {
        auto sp = wrap.PoolMakeShared<allocatorTestClass>(42);
        wp = sp;
        ASSERT_FALSE(wp.expired());
        ASSERT_EQ(42u, wp.lock()->data_);
    }
    // Object is gone, but the control block (also pool-allocated) must still
    // be valid for weak_ptr to query expired() / lock() safely.
    ASSERT_TRUE(wp.expired());
    ASSERT_EQ(nullptr, wp.lock());
    ASSERT_EQ(0u, kallocatorTestValue);
    // wp itself goes out of scope here: control block deallocates back to
    // the pool. The pool, owned by `wrap`, outlives this scope so the
    // contract holds.
}

TEST(allocatorTest, pool_std_allocator_equality) {
    auto allocator_a = std::shared_ptr<IAllocator>(new PoolAllocator());
    auto allocator_b = std::shared_ptr<IAllocator>(new PoolAllocator());
    PoolStdAllocator<int> a1(allocator_a.get());
    PoolStdAllocator<int> a2(allocator_a.get());
    PoolStdAllocator<int> b1(allocator_b.get());
    // Equality is defined by the underlying IAllocator*, which is what
    // std::allocator_traits and STL containers rely on for safe rebinding.
    ASSERT_TRUE(a1 == a2);
    ASSERT_FALSE(a1 == b1);
    ASSERT_TRUE(a1 != b1);
    // Rebound allocator must compare equal to the original (same pool).
    PoolStdAllocator<char> a1_rebound(a1);
    ASSERT_EQ(a1.GetAllocator(), a1_rebound.GetAllocator());
}

TEST(allocatorTest, pool_new_share_ptr_uses_pooled_control_block) {
    // PoolNewSharePtr is now implemented in terms of PoolMakeShared, so it
    // should behave identically (same dtor counting, same use_count semantics)
    // even though it remains [[deprecated]] for clarity at call sites.
    allocatorWrap wrap(std::shared_ptr<IAllocator>(new PoolAllocator()));
    {
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
        auto sp = wrap.PoolNewSharePtr<allocatorTestClass>(99);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
        ASSERT_EQ(99u, sp->data_);
        ASSERT_EQ(1u, kallocatorTestValue);
    }
    ASSERT_EQ(0u, kallocatorTestValue);
}

}  // namespace
}  // namespace common
}  // namespace quicx