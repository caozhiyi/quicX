#include <gtest/gtest.h>
#include "quic/stream/buffer_segment.h"


namespace quicx {
namespace {
    
TEST(buffer_segment_utest, buffer) {
    BufferSegment segment;
    
    EXPECT_TRUE(segment.UpdateLimitOffset(200));
    EXPECT_FALSE(segment.UpdateLimitOffset(100));

    EXPECT_FALSE(segment.Remove(20));
    EXPECT_EQ(segment.ContinuousLength(), 0);

    EXPECT_TRUE(segment.Insert(0, 50));

    EXPECT_EQ(segment.ContinuousLength(), 50);

    EXPECT_FALSE(segment.Insert(20, 50));

    EXPECT_TRUE(segment.Insert(50, 50));

    EXPECT_EQ(segment.ContinuousLength(), 100);

    EXPECT_EQ(segment.Remove(20), 20);

    EXPECT_EQ(segment.ContinuousLength(), 80);

    EXPECT_FALSE(segment.Insert(10, 80));

    EXPECT_EQ(segment.Remove(100), 80);
}

}
}
