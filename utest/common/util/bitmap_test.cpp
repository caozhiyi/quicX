#include <gtest/gtest.h>
#include "common/util/bitmap.h"

namespace quicx {
namespace common {
namespace {

TEST(bitmap_utest, init) {
    Bitmap bm;
    EXPECT_TRUE(bm.Init(64));
    EXPECT_TRUE(bm.Init(100));
}

TEST(bitmap_utest, insert) {
    Bitmap bm;

    EXPECT_TRUE(bm.Init(100));

    EXPECT_TRUE(bm.Insert(10));
    EXPECT_TRUE(bm.Insert(65));
}

TEST(bitmap_utest, remove) {
    Bitmap bm;

    EXPECT_TRUE(bm.Init(100));

    EXPECT_TRUE(bm.Insert(50));
    EXPECT_TRUE(bm.Insert(100));

    EXPECT_TRUE(bm.Remove(100));
    EXPECT_TRUE(bm.Remove(20));
}


TEST(bitmap_utest, minafter) {
    Bitmap bm;
    EXPECT_TRUE(bm.Init(100));

    EXPECT_TRUE(bm.Insert(20));
    EXPECT_TRUE(bm.Insert(40));
    EXPECT_TRUE(bm.Insert(60));
    EXPECT_TRUE(bm.Insert(80));

    EXPECT_EQ(20, bm.GetMinAfter(8));
    EXPECT_TRUE(bm.Remove(20));
    EXPECT_EQ(40, bm.GetMinAfter(8));
    EXPECT_EQ(80, bm.GetMinAfter(61));
}

}
}
}