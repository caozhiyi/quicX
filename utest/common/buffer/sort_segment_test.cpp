#include <gtest/gtest.h>
#include "common/buffer/buffer_sort_chains.h"


namespace quicx {
namespace {
    
TEST(sort_segment_utest, buffer) {
    SortSegment segment;
    
    EXPECT_FALSE(segment.Remove(20));
    EXPECT_EQ(segment.MaxSortLength(), 0);

    EXPECT_TRUE(segment.Insert(1000, 50));

    EXPECT_EQ(segment.MaxSortLength(), 50);

    EXPECT_FALSE(segment.Insert(1010, 50));

    EXPECT_TRUE(segment.Insert(1050, 50));

    EXPECT_EQ(segment.MaxSortLength(), 100);

    EXPECT_TRUE(segment.Remove(20));

    EXPECT_EQ(segment.MaxSortLength(), 80);
}

}
}
