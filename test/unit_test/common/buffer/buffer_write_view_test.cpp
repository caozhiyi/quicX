#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "common/buffer/buffer_span.h"
#include "common/buffer/buffer_write_view.h"

namespace quicx {
namespace common {
namespace {

// Test: Constructor and Valid()
TEST(BufferWriteViewTest, ConstructorAndValid) {
    // Default constructor - invalid
    BufferWriteView view1;
    EXPECT_FALSE(view1.Valid());
    EXPECT_EQ(nullptr, view1.GetData());
    EXPECT_EQ(0u, view1.GetFreeLength());
    EXPECT_EQ(0u, view1.GetDataLength());
    
    // Constructor with buffer and length
    std::array<uint8_t, 32> storage;
    BufferWriteView view2(storage.data(), storage.size());
    EXPECT_TRUE(view2.Valid());
    EXPECT_EQ(storage.data(), view2.GetData());
    EXPECT_EQ(32u, view2.GetFreeLength());
    EXPECT_EQ(0u, view2.GetDataLength());
    
    // Constructor with start and end pointers
    BufferWriteView view3(storage.data(), storage.data() + storage.size());
    EXPECT_TRUE(view3.Valid());
    EXPECT_EQ(storage.data(), view3.GetData());
    EXPECT_EQ(32u, view3.GetFreeLength());
}

// Test: Constructor with invalid range
TEST(BufferWriteViewTest, ConstructorInvalidRange) {
    std::array<uint8_t, 16> storage;
    
    // Reversed pointers
    BufferWriteView view1(storage.data() + 10, storage.data());
    EXPECT_FALSE(view1.Valid());
    
    // Nullptr
    BufferWriteView view2(nullptr, static_cast<uint32_t>(10));
    EXPECT_FALSE(view2.Valid());
    
    // Zero length is valid (edge case)
    BufferWriteView view3(storage.data(), static_cast<uint32_t>(0));
    EXPECT_TRUE(view3.Valid());
    EXPECT_EQ(0u, view3.GetFreeLength());
}

// Test: Reset()
TEST(BufferWriteViewTest, Reset) {
    std::array<uint8_t, 32> storage1;
    std::array<uint8_t, 64> storage2;
    
    BufferWriteView view(storage1.data(), storage1.size());
    EXPECT_TRUE(view.Valid());
    EXPECT_EQ(32u, view.GetFreeLength());
    
    // Write some data
    std::vector<uint8_t> data = {1, 2, 3, 4};
    view.Write(data.data(), data.size());
    EXPECT_EQ(4u, view.GetDataLength());
    
    // Reset to new buffer
    view.Reset(storage2.data(), storage2.size());
    EXPECT_TRUE(view.Valid());
    EXPECT_EQ(64u, view.GetFreeLength());
    EXPECT_EQ(0u, view.GetDataLength());  // Should reset data length
    
    // Reset with pointers
    view.Reset(storage1.data(), storage1.data() + storage1.size());
    EXPECT_TRUE(view.Valid());
    EXPECT_EQ(32u, view.GetFreeLength());
}

// Test: Reset() with invalid range
TEST(BufferWriteViewTest, ResetInvalid) {
    std::array<uint8_t, 32> storage;
    BufferWriteView view(storage.data(), storage.size());
    EXPECT_TRUE(view.Valid());
    
    // Reset with nullptr
    view.Reset(nullptr, static_cast<uint32_t>(10));
    EXPECT_FALSE(view.Valid());
    EXPECT_EQ(nullptr, view.GetData());
    
    // Reset with reversed pointers
    view.Reset(storage.data() + 10, storage.data());
    EXPECT_FALSE(view.Valid());
}

// Test: Write() basic operations
TEST(BufferWriteViewTest, WriteBasic) {
    std::array<uint8_t, 32> storage;
    std::fill(storage.begin(), storage.end(), 0);
    BufferWriteView view(storage.data(), storage.size());
    
    // Write small data
    std::vector<uint8_t> data1 = {1, 2, 3, 4};
    EXPECT_EQ(4u, view.Write(data1.data(), data1.size()));
    EXPECT_EQ(4u, view.GetDataLength());
    EXPECT_EQ(28u, view.GetFreeLength());
    
    // Verify data was written
    EXPECT_EQ(0, std::memcmp(storage.data(), data1.data(), data1.size()));
    
    // Write more data
    std::vector<uint8_t> data2 = {5, 6, 7, 8, 9};
    EXPECT_EQ(5u, view.Write(data2.data(), data2.size()));
    EXPECT_EQ(9u, view.GetDataLength());
    EXPECT_EQ(23u, view.GetFreeLength());
    
    // Verify all data
    std::vector<uint8_t> expected = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    EXPECT_EQ(0, std::memcmp(storage.data(), expected.data(), expected.size()));
}

// Test: Write() with nullptr
TEST(BufferWriteViewTest, WriteNullptr) {
    std::array<uint8_t, 32> storage;
    BufferWriteView view(storage.data(), storage.size());
    
    EXPECT_EQ(0u, view.Write(nullptr, 10));
    EXPECT_EQ(0u, view.GetDataLength());
}

// Test: Write() with zero length
TEST(BufferWriteViewTest, WriteZeroLength) {
    std::array<uint8_t, 32> storage;
    BufferWriteView view(storage.data(), storage.size());
    
    std::vector<uint8_t> data = {1, 2, 3};
    EXPECT_EQ(0u, view.Write(data.data(), 0));
    EXPECT_EQ(0u, view.GetDataLength());
}

// Test: Write() exceeding capacity
TEST(BufferWriteViewTest, WriteExceedingCapacity) {
    std::array<uint8_t, 8> storage;
    BufferWriteView view(storage.data(), storage.size());
    
    std::vector<uint8_t> large_data(20);
    std::fill(large_data.begin(), large_data.end(), 0xAA);
    
    // Should only write 8 bytes (buffer capacity)
    EXPECT_EQ(8u, view.Write(large_data.data(), large_data.size()));
    EXPECT_EQ(8u, view.GetDataLength());
    EXPECT_EQ(0u, view.GetFreeLength());
    
    // Try to write more - should fail
    std::vector<uint8_t> more_data = {1, 2, 3};
    EXPECT_EQ(0u, view.Write(more_data.data(), more_data.size()));
}

// Test: Write() on invalid view
TEST(BufferWriteViewTest, WriteOnInvalid) {
    BufferWriteView view;  // Invalid
    
    std::vector<uint8_t> data = {1, 2, 3};
    EXPECT_EQ(0u, view.Write(data.data(), data.size()));
}

// Test: MoveWritePt() forward
TEST(BufferWriteViewTest, MoveWritePtForward) {
    std::array<uint8_t, 32> storage;
    BufferWriteView view(storage.data(), storage.size());
    
    // Reserve 10 bytes
    EXPECT_EQ(10u, view.MoveWritePt(10));
    EXPECT_EQ(10u, view.GetDataLength());
    EXPECT_EQ(22u, view.GetFreeLength());
    
    // Reserve 5 more bytes
    EXPECT_EQ(5u, view.MoveWritePt(5));
    EXPECT_EQ(15u, view.GetDataLength());
    EXPECT_EQ(17u, view.GetFreeLength());
}

// Test: MoveWritePt() forward beyond capacity
TEST(BufferWriteViewTest, MoveWritePtForwardBeyond) {
    std::array<uint8_t, 16> storage;
    BufferWriteView view(storage.data(), storage.size());
    
    // Try to reserve 100 bytes (more than capacity)
    EXPECT_EQ(16u, view.MoveWritePt(100));
    EXPECT_EQ(16u, view.GetDataLength());
    EXPECT_EQ(0u, view.GetFreeLength());
}

// Test: MoveWritePt() backward
TEST(BufferWriteViewTest, MoveWritePtBackward) {
    std::array<uint8_t, 32> storage;
    BufferWriteView view(storage.data(), storage.size());
    
    // Write some data
    std::vector<uint8_t> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    view.Write(data.data(), data.size());
    EXPECT_EQ(10u, view.GetDataLength());
    
    // Move write pointer back 4 bytes
    EXPECT_EQ(4u, view.MoveWritePt(-4));
    EXPECT_EQ(6u, view.GetDataLength());
    EXPECT_EQ(26u, view.GetFreeLength());
}

// Test: MoveWritePt() backward beyond start
TEST(BufferWriteViewTest, MoveWritePtBackwardBeyond) {
    std::array<uint8_t, 32> storage;
    BufferWriteView view(storage.data(), storage.size());
    
    // Write 10 bytes
    view.MoveWritePt(10);
    
    // Try to move backward 20 bytes (more than written)
    EXPECT_EQ(10u, view.MoveWritePt(-20));
    EXPECT_EQ(0u, view.GetDataLength());
    EXPECT_EQ(32u, view.GetFreeLength());
}

// Test: MoveWritePt() zero
TEST(BufferWriteViewTest, MoveWritePtZero) {
    std::array<uint8_t, 32> storage;
    BufferWriteView view(storage.data(), storage.size());
    
    view.MoveWritePt(5);
    EXPECT_EQ(5u, view.GetDataLength());
    
    // Move by zero
    EXPECT_EQ(0u, view.MoveWritePt(0));
    EXPECT_EQ(5u, view.GetDataLength());
}

// Test: MoveWritePt() on invalid view
TEST(BufferWriteViewTest, MoveWritePtOnInvalid) {
    BufferWriteView view;  // Invalid
    
    EXPECT_EQ(0u, view.MoveWritePt(10));
    EXPECT_EQ(0u, view.MoveWritePt(-5));
}

// Test: GetData()
TEST(BufferWriteViewTest, GetData) {
    std::array<uint8_t, 32> storage;
    BufferWriteView view(storage.data(), storage.size());
    
    EXPECT_EQ(storage.data(), view.GetData());
    
    // After writing, GetData should point to next writable location
    std::vector<uint8_t> data = {1, 2, 3, 4};
    view.Write(data.data(), data.size());
    EXPECT_EQ(storage.data() + 4, view.GetData());
}

// Test: GetFreeLength()
TEST(BufferWriteViewTest, GetFreeLength) {
    std::array<uint8_t, 64> storage;
    BufferWriteView view(storage.data(), storage.size());
    
    EXPECT_EQ(64u, view.GetFreeLength());
    
    view.MoveWritePt(20);
    EXPECT_EQ(44u, view.GetFreeLength());
    
    view.MoveWritePt(30);
    EXPECT_EQ(14u, view.GetFreeLength());
    
    view.MoveWritePt(14);
    EXPECT_EQ(0u, view.GetFreeLength());
}

// Test: GetDataLength()
TEST(BufferWriteViewTest, GetDataLength) {
    std::array<uint8_t, 32> storage;
    BufferWriteView view(storage.data(), storage.size());
    
    EXPECT_EQ(0u, view.GetDataLength());
    
    std::vector<uint8_t> data1 = {1, 2, 3, 4, 5};
    view.Write(data1.data(), data1.size());
    EXPECT_EQ(5u, view.GetDataLength());
    
    std::vector<uint8_t> data2 = {6, 7, 8};
    view.Write(data2.data(), data2.size());
    EXPECT_EQ(8u, view.GetDataLength());
}

// Test: GetWritableSpan()
TEST(BufferWriteViewTest, GetWritableSpan) {
    std::array<uint8_t, 32> storage;
    BufferWriteView view(storage.data(), storage.size());
    
    // Initially all space is writable
    auto span1 = view.GetWritableSpan();
    EXPECT_EQ(32u, span1.GetLength());
    EXPECT_EQ(storage.data(), span1.GetStart());
    
    // Write some data
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    view.Write(data.data(), data.size());
    
    // Writable space reduced
    auto span2 = view.GetWritableSpan();
    EXPECT_EQ(27u, span2.GetLength());
    EXPECT_EQ(storage.data() + 5, span2.GetStart());
}

// Test: GetWritableSpan() on invalid view
TEST(BufferWriteViewTest, GetWritableSpanInvalid) {
    BufferWriteView view;  // Invalid
    
    auto span = view.GetWritableSpan();
    EXPECT_FALSE(span.Valid());
    EXPECT_EQ(0u, span.GetLength());
}

// Test: GetWrittenSpan()
TEST(BufferWriteViewTest, GetWrittenSpan) {
    std::array<uint8_t, 32> storage;
    std::fill(storage.begin(), storage.end(), 0);
    BufferWriteView view(storage.data(), storage.size());
    
    // Initially no data written
    auto span1 = view.GetWrittenSpan();
    EXPECT_EQ(0u, span1.GetLength());
    
    // Write some data
    std::vector<uint8_t> data = {10, 20, 30, 40, 50};
    view.Write(data.data(), data.size());
    
    // Written span should reflect written data
    auto span2 = view.GetWrittenSpan();
    EXPECT_EQ(5u, span2.GetLength());
    EXPECT_EQ(storage.data(), span2.GetStart());
    EXPECT_EQ(0, std::memcmp(span2.GetStart(), data.data(), data.size()));
}

// Test: GetWrittenSpan() on invalid view
TEST(BufferWriteViewTest, GetWrittenSpanInvalid) {
    BufferWriteView view;  // Invalid
    
    auto span = view.GetWrittenSpan();
    EXPECT_FALSE(span.Valid());
    EXPECT_EQ(0u, span.GetLength());
}

// Test: Sequential write operations
TEST(BufferWriteViewTest, SequentialWrites) {
    std::array<uint8_t, 64> storage;
    std::fill(storage.begin(), storage.end(), 0);
    BufferWriteView view(storage.data(), storage.size());
    
    // Write 1
    std::vector<uint8_t> data1 = {1, 2, 3};
    view.Write(data1.data(), data1.size());
    
    // Write 2
    std::vector<uint8_t> data2 = {4, 5, 6, 7};
    view.Write(data2.data(), data2.size());
    
    // Write 3
    std::vector<uint8_t> data3 = {8, 9};
    view.Write(data3.data(), data3.size());
    
    EXPECT_EQ(9u, view.GetDataLength());
    EXPECT_EQ(55u, view.GetFreeLength());
    
    // Verify all data
    std::vector<uint8_t> expected = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    EXPECT_EQ(0, std::memcmp(storage.data(), expected.data(), expected.size()));
}

// Test: Interleaved write and move operations
TEST(BufferWriteViewTest, InterleavedWriteAndMove) {
    std::array<uint8_t, 32> storage;
    std::fill(storage.begin(), storage.end(), 0);
    BufferWriteView view(storage.data(), storage.size());
    
    // Write some data
    std::vector<uint8_t> data1 = {1, 2, 3, 4};
    view.Write(data1.data(), data1.size());
    EXPECT_EQ(4u, view.GetDataLength());
    
    // Reserve space
    view.MoveWritePt(3);
    EXPECT_EQ(7u, view.GetDataLength());
    
    // Write more
    std::vector<uint8_t> data2 = {5, 6};
    view.Write(data2.data(), data2.size());
    EXPECT_EQ(9u, view.GetDataLength());
    
    // Shrink
    view.MoveWritePt(-2);
    EXPECT_EQ(7u, view.GetDataLength());
}

// Test: Large data operations
TEST(BufferWriteViewTest, LargeDataOperations) {
    std::vector<uint8_t> storage(4096);
    BufferWriteView view(storage.data(), storage.size());
    
    // Write large data
    std::vector<uint8_t> large_data(4096);
    for (size_t i = 0; i < large_data.size(); ++i) {
        large_data[i] = static_cast<uint8_t>(i % 256);
    }
    
    EXPECT_EQ(4096u, view.Write(large_data.data(), large_data.size()));
    EXPECT_EQ(4096u, view.GetDataLength());
    EXPECT_EQ(0u, view.GetFreeLength());
    
    // Verify data
    EXPECT_EQ(0, std::memcmp(storage.data(), large_data.data(), large_data.size()));
}

// Test: Boundary conditions
TEST(BufferWriteViewTest, BoundaryConditions) {
    std::array<uint8_t, 16> storage;
    std::fill(storage.begin(), storage.end(), 0);
    BufferWriteView view(storage.data(), storage.size());
    
    // Write exactly buffer size
    std::vector<uint8_t> data(16);
    std::fill(data.begin(), data.end(), 0x55);
    EXPECT_EQ(16u, view.Write(data.data(), data.size()));
    EXPECT_EQ(16u, view.GetDataLength());
    EXPECT_EQ(0u, view.GetFreeLength());
    
    // Verify data
    EXPECT_EQ(0, std::memcmp(storage.data(), data.data(), data.size()));
}

// Test: Write after move backward
TEST(BufferWriteViewTest, WriteAfterMoveBackward) {
    std::array<uint8_t, 32> storage;
    std::fill(storage.begin(), storage.end(), 0);
    BufferWriteView view(storage.data(), storage.size());
    
    // Write initial data
    std::vector<uint8_t> data1 = {1, 2, 3, 4, 5, 6, 7, 8};
    view.Write(data1.data(), data1.size());
    EXPECT_EQ(8u, view.GetDataLength());
    
    // Move back
    view.MoveWritePt(-3);
    EXPECT_EQ(5u, view.GetDataLength());
    
    // Write new data (should overwrite)
    std::vector<uint8_t> data2 = {9, 10, 11};
    view.Write(data2.data(), data2.size());
    EXPECT_EQ(8u, view.GetDataLength());
    
    // Verify data
    std::vector<uint8_t> expected = {1, 2, 3, 4, 5, 9, 10, 11};
    EXPECT_EQ(0, std::memcmp(storage.data(), expected.data(), expected.size()));
}

// Test: Multiple reset cycles
TEST(BufferWriteViewTest, MultipleResetCycles) {
    std::array<uint8_t, 32> storage1;
    std::array<uint8_t, 32> storage2;
    std::fill(storage1.begin(), storage1.end(), 0);
    std::fill(storage2.begin(), storage2.end(), 0);
    
    BufferWriteView view(storage1.data(), storage1.size());
    
    // Cycle 1
    std::vector<uint8_t> data1 = {1, 2, 3};
    view.Write(data1.data(), data1.size());
    EXPECT_EQ(3u, view.GetDataLength());
    
    // Reset to storage2
    view.Reset(storage2.data(), storage2.size());
    EXPECT_EQ(0u, view.GetDataLength());
    EXPECT_EQ(32u, view.GetFreeLength());
    
    // Cycle 2
    std::vector<uint8_t> data2 = {4, 5, 6, 7};
    view.Write(data2.data(), data2.size());
    EXPECT_EQ(4u, view.GetDataLength());
    
    // Verify storage1 has data1
    EXPECT_EQ(0, std::memcmp(storage1.data(), data1.data(), data1.size()));
    
    // Verify storage2 has data2
    EXPECT_EQ(0, std::memcmp(storage2.data(), data2.data(), data2.size()));
}

}
}
}


