#include <gtest/gtest.h>
#include "common/buffer/buffer.h"
#include "http3/frame/qpack_encoder_frames.h"

using namespace quicx::http3;

TEST(QpackEncoderFramesTest, SetCapacityEncodeDecode) {
    QpackSetCapacityFrame f1;
    f1.SetCapacity(4096);
    uint8_t bufmem[32] = {0}; 
    auto buf = std::make_shared<quicx::common::Buffer>(bufmem, sizeof(bufmem));
    ASSERT_TRUE(f1.Encode(buf));
    
    // Create a read view for decoding
    auto read_buf = buf->GetReadViewPtr();

    QpackSetCapacityFrame f2;
    ASSERT_TRUE(f2.Decode(read_buf));
    EXPECT_EQ(f2.GetCapacity(), 4096u);
}

TEST(QpackEncoderFramesTest, InsertWithNameRefEncodeDecode) {
    QpackInsertWithNameRefFrame f1; f1.Set(true, 10, "value");
    uint8_t bufmem[128] = {0}; auto buf = std::make_shared<quicx::common::Buffer>(bufmem, sizeof(bufmem));
    ASSERT_TRUE(f1.Encode(buf));
    
    // Create a read view for decoding
    auto read_buf = buf->GetReadViewPtr();

    QpackInsertWithNameRefFrame f2;
    ASSERT_TRUE(f2.Decode(read_buf));
    EXPECT_TRUE(f2.IsStatic());
    EXPECT_EQ(f2.GetNameIndex(), 10u);
    EXPECT_EQ(f2.GetValue(), std::string("value"));
}

TEST(QpackEncoderFramesTest, InsertWithoutNameRefEncodeDecode) {
    QpackInsertWithoutNameRefFrame f1;
    f1.Set("name", "value");

    uint8_t bufmem[128] = {0}; 
    auto buf = std::make_shared<quicx::common::Buffer>(bufmem, sizeof(bufmem));
    ASSERT_TRUE(f1.Encode(buf));
    
    // Create a read view for decoding
    auto read_buf = buf->GetReadViewPtr();

    QpackInsertWithoutNameRefFrame f2;
    ASSERT_TRUE(f2.Decode(read_buf));
    EXPECT_EQ(f2.GetName(), std::string("name"));
    EXPECT_EQ(f2.GetValue(), std::string("value"));
}

TEST(QpackEncoderFramesTest, DuplicateEncodeDecode) {
    QpackDuplicateFrame f1; f1.Set(7);
    uint8_t bufmem[32] = {0}; auto buf = std::make_shared<quicx::common::Buffer>(bufmem, sizeof(bufmem));
    ASSERT_TRUE(f1.Encode(buf));
    
    // Create a read view for decoding
    auto read_buf = buf->GetReadViewPtr();

    QpackDuplicateFrame f2;
    ASSERT_TRUE(f2.Decode(read_buf));
    EXPECT_EQ(f2.Get(), 7u);
}


