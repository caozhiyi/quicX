#include "gtest/gtest.h"
#include "common/util/bitmap.h"

using namespace quicx;

TEST(Bitmap_test, case1) {
    Bitmap bp;
    EXPECT_TRUE(bp.Init(50));
    EXPECT_TRUE(bp.Insert(15));
    EXPECT_TRUE(bp.Insert(24));
}
