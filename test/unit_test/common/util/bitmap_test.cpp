#include "common/util/bitmap.h"
#include <gtest/gtest.h>

namespace quicx {
namespace common {
namespace {

TEST(BitmapTest, init) {
    Bitmap bm;
    EXPECT_TRUE(bm.Init(64));
    EXPECT_TRUE(bm.Init(100));
}

TEST(BitmapTest, insert) {
    Bitmap bm;

    EXPECT_TRUE(bm.Init(100));

    EXPECT_TRUE(bm.Insert(10));
    EXPECT_TRUE(bm.Insert(65));
}

TEST(BitmapTest, remove) {
    Bitmap bm;

    EXPECT_TRUE(bm.Init(100));

    EXPECT_TRUE(bm.Insert(50));
    EXPECT_TRUE(bm.Insert(100));

    EXPECT_TRUE(bm.Remove(100));
    EXPECT_TRUE(bm.Remove(20));
}

TEST(BitmapTest, minafter) {
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

// The cross-word scan used to skip bit 0 of the word it landed on, so a word holding
// both bit 0 and a higher bit resolved to the higher one. Existing coverage missed it
// because every follow-up word it built had its lowest bit above 0.
TEST(BitmapTest, minafter_lands_on_first_bit_of_next_word) {
    Bitmap bm;
    EXPECT_TRUE(bm.Init(200));

    EXPECT_TRUE(bm.Insert(10));
    EXPECT_TRUE(bm.Insert(64));  // bit 0 of word 1
    EXPECT_TRUE(bm.Insert(66));  // bit 2 of word 1, so bit 0 cannot be skipped silently

    EXPECT_EQ(10, bm.GetMinAfter(0));
    EXPECT_EQ(64, bm.GetMinAfter(11));
    EXPECT_EQ(64, bm.GetMinAfter(63));
}

TEST(BitmapTest, minafter_returns_negative_when_nothing_follows) {
    Bitmap bm;
    EXPECT_TRUE(bm.Init(200));

    EXPECT_TRUE(bm.Insert(5));

    EXPECT_EQ(5, bm.GetMinAfter(0));
    EXPECT_EQ(-1, bm.GetMinAfter(6));
}

// Clear() picked the wrong word index for the same reason, leaving word 0 populated.
// Empty() only consults the summary bitmap, so the stale bits stayed invisible until a
// later insert made GetMinAfter walk word 0 again.
TEST(BitmapTest, clear_drops_every_word_including_the_first) {
    Bitmap bm;
    EXPECT_TRUE(bm.Init(200));

    EXPECT_TRUE(bm.Insert(0));  // bit 0 of word 0
    EXPECT_TRUE(bm.Insert(70));
    EXPECT_TRUE(bm.Insert(150));
    EXPECT_FALSE(bm.Empty());

    bm.Clear();
    EXPECT_TRUE(bm.Empty());
    EXPECT_EQ(-1, bm.GetMinAfter(0));

    // Reusing the bitmap must not resurrect the cleared index 0.
    EXPECT_TRUE(bm.Insert(70));
    EXPECT_EQ(70, bm.GetMinAfter(0));
}

}  // namespace
}  // namespace common
}  // namespace quicx