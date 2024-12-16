#include <gtest/gtest.h>
#include "common/buffer/buffer.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace common {
namespace {

class BufferWrapperTest : public testing::Test {
protected:
    void SetUp() override {
        buffer_ = std::make_shared<Buffer>(buf_, uint32_t(sizeof(buf_)));
    }

    uint8_t buf_[1024];
    std::shared_ptr<Buffer> buffer_;
};

TEST_F(BufferWrapperTest, WriteReadUint8) {
    uint8_t write_value = 0x42;
    uint8_t read_value = 0;

    {
        BufferEncodeWrapper write_wrapper(buffer_);
        EXPECT_TRUE(write_wrapper.EncodeFixedUint8(write_value));
    }
    BufferDecodeWrapper read_wrapper(buffer_);

    EXPECT_TRUE(read_wrapper.DecodeFixedUint8(read_value));
    EXPECT_EQ(write_value, read_value);
}

TEST_F(BufferWrapperTest, WriteReadUint16) {
    uint16_t write_value = 0x4242;
    uint16_t read_value = 0;

    {
        BufferEncodeWrapper write_wrapper(buffer_);
        EXPECT_TRUE(write_wrapper.EncodeFixedUint16(write_value));
    }
    BufferDecodeWrapper read_wrapper(buffer_);

    EXPECT_TRUE(read_wrapper.DecodeFixedUint16(read_value));
    EXPECT_EQ(write_value, read_value);
}

TEST_F(BufferWrapperTest, WriteReadUint32) {
    uint32_t write_value = 0x42424242;
    uint32_t read_value = 0;
    {
        BufferEncodeWrapper write_wrapper(buffer_);
        EXPECT_TRUE(write_wrapper.EncodeFixedUint32(write_value));
    }
    BufferDecodeWrapper read_wrapper(buffer_);

    EXPECT_TRUE(read_wrapper.DecodeFixedUint32(read_value)); 
    EXPECT_EQ(write_value, read_value);
}

TEST_F(BufferWrapperTest, WriteReadUint64) {
    uint64_t write_value = 0x4242424242424242;
    uint64_t read_value = 0;
    {
        BufferEncodeWrapper write_wrapper(buffer_);
        EXPECT_TRUE(write_wrapper.EncodeFixedUint64(write_value));
    }
    BufferDecodeWrapper read_wrapper(buffer_);
    
    EXPECT_TRUE(read_wrapper.DecodeFixedUint64(read_value)); 
    EXPECT_EQ(write_value, read_value);
}

TEST_F(BufferWrapperTest, WriteReadVarint) {
    uint64_t write_value1 = 63;  // 0x3F
    uint64_t write_value2 = 16383;  // 0x3FFF
    uint64_t write_value3 = 1073741823;  // 0x3FFFFFFF
    uint64_t write_value4 = 4611686018427387903;  // 0x3FFFFFFFFFFFFFFF
    uint64_t read_value1 = 0;
    uint64_t read_value2 = 0;
    uint64_t read_value3 = 0;
    uint64_t read_value4 = 0;

    {
        BufferEncodeWrapper write_wrapper(buffer_);
        EXPECT_TRUE(write_wrapper.EncodeVarint(write_value1));
        EXPECT_TRUE(write_wrapper.EncodeVarint(write_value2));
        EXPECT_TRUE(write_wrapper.EncodeVarint(write_value3));
        EXPECT_TRUE(write_wrapper.EncodeVarint(write_value4));
    }
    
    BufferDecodeWrapper read_wrapper(buffer_);

    EXPECT_TRUE(read_wrapper.DecodeVarint(read_value1));
    EXPECT_TRUE(read_wrapper.DecodeVarint(read_value2));
    EXPECT_TRUE(read_wrapper.DecodeVarint(read_value3));
    EXPECT_TRUE(read_wrapper.DecodeVarint(read_value4));
    
    EXPECT_EQ(write_value1, read_value1);
    EXPECT_EQ(write_value2, read_value2);
    EXPECT_EQ(write_value3, read_value3);
    EXPECT_EQ(write_value4, read_value4);
}

TEST_F(BufferWrapperTest, WriteReadBytes) {
    std::vector<uint8_t> write_data = {0x42, 0x43, 0x44, 0x45};
    std::vector<uint8_t> read_data(write_data.size());

    {
        BufferEncodeWrapper write_wrapper(buffer_);
        write_wrapper.EncodeBytes(write_data.data(), write_data.size());
    }
    BufferDecodeWrapper read_wrapper(buffer_);
    
    auto data = (uint8_t*)read_data.data();
    EXPECT_TRUE(read_wrapper.DecodeBytes(data, read_data.size()));
    EXPECT_EQ(write_data, read_data);
}

// TEST_F(BufferWrapperTest, WriteReadString) {
//     std::string write_str = "Hello, World!";
//     std::string read_str;

//     {
//         BufferEncodeWrapper write_wrapper(buffer_);
//         EXPECT_TRUE(write_wrapper.EncodeString(write_str));
//     }
//     BufferDecodeWrapper read_wrapper(buffer_);
    
//     EXPECT_TRUE(read_wrapper.DecodeString(write_str.size(), read_str));
//     EXPECT_EQ(write_str, read_str);
// }

TEST_F(BufferWrapperTest, BufferBoundaryCheck) {
    // Try to read when buffer is empty
    BufferDecodeWrapper read_wrapper(buffer_);
    uint8_t value;
    EXPECT_FALSE(read_wrapper.DecodeFixedUint8(value));
    
    // Write at buffer capacity
    BufferEncodeWrapper write_wrapper(buffer_);
    std::vector<uint8_t> large_data(1024, 0x42);
    EXPECT_TRUE(write_wrapper.EncodeBytes(large_data.data(), large_data.size()));
    
    // Try to write beyond capacity
    EXPECT_FALSE(write_wrapper.EncodeFixedUint8(0x42));
}

}  // namespace
}  // namespace common
}  // namespace quicx