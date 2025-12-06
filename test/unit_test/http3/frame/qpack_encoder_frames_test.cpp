#include <gtest/gtest.h>
#include "http3/frame/qpack_encoder_frames.h"
#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

namespace quicx {
namespace http3 {
namespace {

TEST(QpackEncoderFramesTest, SetCapacityEncodeDecode) {
    QpackSetCapacityFrame f1;
    f1.SetCapacity(4096);
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(32);
    auto buf = std::make_shared<common::SingleBlockBuffer>(chunk);
    ASSERT_TRUE(f1.Encode(buf));
    
    QpackSetCapacityFrame f2;
    ASSERT_TRUE(f2.Decode(buf));
    EXPECT_EQ(f2.GetCapacity(), 4096u);
}

TEST(QpackEncoderFramesTest, InsertWithNameRefEncodeDecode) {
    QpackInsertWithNameRefFrame f1; f1.Set(true, 10, "value");
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(128);
    auto buf = std::make_shared<common::SingleBlockBuffer>(chunk);
    ASSERT_TRUE(f1.Encode(buf));
    
    QpackInsertWithNameRefFrame f2;
    ASSERT_TRUE(f2.Decode(buf));
    EXPECT_TRUE(f2.IsStatic());
    EXPECT_EQ(f2.GetNameIndex(), 10u);
    EXPECT_EQ(f2.GetValue(), std::string("value"));
}

TEST(QpackEncoderFramesTest, InsertWithoutNameRefEncodeDecode) {
    QpackInsertWithoutNameRefFrame f1;
    f1.Set("name", "value");

    auto chunk = std::make_shared<common::StandaloneBufferChunk>(128);
    auto buf = std::make_shared<common::SingleBlockBuffer>(chunk);
    ASSERT_TRUE(f1.Encode(buf));
    
    QpackInsertWithoutNameRefFrame f2;
    ASSERT_TRUE(f2.Decode(buf));
    EXPECT_EQ(f2.GetName(), std::string("name"));
    EXPECT_EQ(f2.GetValue(), std::string("value"));
}

TEST(QpackEncoderFramesTest, DuplicateEncodeDecode) {
    QpackDuplicateFrame f1; f1.Set(7);
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(32);
    auto buf = std::make_shared<common::SingleBlockBuffer>(chunk);
    ASSERT_TRUE(f1.Encode(buf));
    
    QpackDuplicateFrame f2;
    ASSERT_TRUE(f2.Decode(buf));
    EXPECT_EQ(f2.Get(), 7u);
}


}
}
}