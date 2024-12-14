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
        auto buffer = std::make_shared<Buffer>(buffer_, uint32_t(sizeof(buffer_)));
        write_wrapper_ = std::make_shared<BufferEncodeWrapper>(buffer);
        read_wrapper_ = std::make_shared<BufferDecodeWrapper>(buffer);
    }

    uint8_t buffer_[1024];
    std::shared_ptr<BufferEncodeWrapper> write_wrapper_;
    std::shared_ptr<BufferDecodeWrapper> read_wrapper_;
};

TEST_F(BufferWrapperTest, WriteReadUint8) {
    uint8_t write_value = 0x42;
    EXPECT_TRUE(write_wrapper_->EncodeFixedUint8(write_value));
    
    uint8_t read_value = 0;
    EXPECT_TRUE(read_wrapper_->DecodeFixedUint8(read_value));
    EXPECT_EQ(write_value, read_value);
}

TEST_F(BufferWrapperTest, WriteReadUint16) {
    uint16_t write_value = 0x4242;
    EXPECT_TRUE(write_wrapper_->EncodeFixedUint16(write_value));
    
    uint16_t read_value = 0;
    EXPECT_TRUE(read_wrapper_->DecodeFixedUint16(read_value));
    EXPECT_EQ(write_value, read_value);
}

TEST_F(BufferWrapperTest, WriteReadUint32) {
    uint32_t write_value = 0x42424242;
    EXPECT_TRUE(write_wrapper_->EncodeFixedUint32(write_value)); 
    
    uint32_t read_value = 0;
    EXPECT_TRUE(read_wrapper_->DecodeFixedUint32(read_value)); 
    EXPECT_EQ(write_value, read_value);
}

TEST_F(BufferWrapperTest, WriteReadUint64) {
    uint64_t write_value = 0x4242424242424242;
    EXPECT_TRUE(write_wrapper_->EncodeFixedUint64(write_value)); 
    
    uint64_t read_value = 0;
    EXPECT_TRUE(read_wrapper_->DecodeFixedUint64(read_value)); 
    EXPECT_EQ(write_value, read_value);
}

TEST_F(BufferWrapperTest, WriteReadVarint) {
    // Test small value (1 byte)
    uint64_t write_value1 = 63;  // 0x3F
    EXPECT_TRUE(write_wrapper_->EncodeVarint(write_value1)); 
    
    uint64_t read_value1 = 0;
    EXPECT_TRUE(read_wrapper_->DecodeVarint(read_value1)); 
    EXPECT_EQ(write_value1, read_value1);

    // Test medium value (2 bytes)
    uint64_t write_value2 = 16383;  // 0x3FFF
    EXPECT_TRUE(write_wrapper_->EncodeVarint(write_value2));
    
    uint64_t read_value2 = 0;
    EXPECT_TRUE(read_wrapper_->DecodeVarint(read_value2));
    EXPECT_EQ(write_value2, read_value2);

    // Test large value (4 bytes)
    uint64_t write_value3 = 1073741823;  // 0x3FFFFFFF
    EXPECT_TRUE(write_wrapper_->EncodeVarint(write_value3));
    
    uint64_t read_value3 = 0;
    EXPECT_TRUE(read_wrapper_->DecodeVarint(read_value3));
    EXPECT_EQ(write_value3, read_value3);

    // Test very large value (8 bytes)
    uint64_t write_value4 = 4611686018427387903;  // 0x3FFFFFFFFFFFFFFF
    EXPECT_TRUE(write_wrapper_->EncodeVarint(write_value4));
    
    uint64_t read_value4 = 0;
    EXPECT_TRUE(read_wrapper_->DecodeVarint(read_value4));
    EXPECT_EQ(write_value4, read_value4);
}

// TEST_F(BufferWrapperTest, WriteReadBytes) {
//     std::vector<uint8_t> write_data = {0x42, 0x43, 0x44, 0x45};
//     EXPECT_TRUE(write_wrapper_->EncodeBytes(write_data.data(), write_data.size()));
    
//     std::vector<uint8_t> read_data(write_data.size());
//     EXPECT_TRUE(read_wrapper_->DecodeBytes((uint8_t*)read_data.data(), read_data.size()));
//     EXPECT_EQ(write_data, read_data);
// }

// TEST_F(BufferWrapperTest, WriteReadString) {
//     std::string write_str = "Hello, World!";
//     EXPECT_TRUE(write_wrapper_->EncodeString(write_str));
    
//     std::string read_str;
//     EXPECT_TRUE(read_wrapper_->DecodeString(write_str.size(), read_str));
//     EXPECT_EQ(write_str, read_str);
// }

// TEST_F(BufferWrapperTest, BufferBoundaryCheck) {
//     // Try to read when buffer is empty
//     uint8_t value;
//     EXPECT_FALSE(read_wrapper_->DecodeFixedUint8(value));
    
//     // Write at buffer capacity
//     std::vector<uint8_t> large_data(1024, 0x42);
//     EXPECT_TRUE(write_wrapper_->EncodeBytes(large_data.data(), large_data.size()));
    
//     // Try to write beyond capacity
//     EXPECT_FALSE(write_wrapper_->EncodeFixedUint8(0x42));
// }

// TEST_F(BufferWrapperTest, ReadWritePosition) {
//     uint32_t write_value = 0x42424242;
//     EXPECT_TRUE(write_wrapper_->EncodeFixedUint32(write_value));
    
//     // Check write position
//     EXPECT_EQ(write_wrapper_->GetWriteIndex(), 4);
    
//     uint32_t read_value;
//     EXPECT_TRUE(read_wrapper_->DecodeFixedUint32(read_value));
    
//     // Check read position
//     EXPECT_EQ(read_wrapper_->GetReadIndex(), 4);
// }


}  // namespace
}  // namespace common
}  // namespace quicx