#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_chunk.h"
#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

namespace quicx {
namespace common {
namespace {

std::shared_ptr<BufferChunk> MakeChunk(uint32_t size = 64) {
    auto pool = MakeBlockMemoryPoolPtr(size, /*add_num=*/1u);
    return std::make_shared<BufferChunk>(pool);
}

std::shared_ptr<StandaloneBufferChunk> MakeStandaloneChunk(uint32_t size = 64) {
    return std::make_shared<StandaloneBufferChunk>(size);
}

TEST(SingleBlockBufferTest, WriteReadCycleAndVisit) {
    auto chunk = MakeChunk(32);
    ASSERT_TRUE(chunk->Valid());

    SingleBlockBuffer buffer(chunk);

    std::array<uint8_t, 8> payload = {1, 2, 3, 4, 5, 6, 7, 8};
    EXPECT_EQ(payload.size(), buffer.Write(payload.data(), payload.size()));
    EXPECT_EQ(payload.size(), buffer.GetDataLength());

    auto view = buffer.GetReadView();
    ASSERT_TRUE(view.Valid());
    EXPECT_EQ(payload.size(), view.GetDataLength());

    auto span = buffer.GetReadableSpan();
    EXPECT_EQ(payload.size(), span.GetLength());
    auto shared = buffer.GetSharedReadableSpan();
    ASSERT_TRUE(shared.Valid());
    EXPECT_EQ(payload.size(), shared.GetLength());

    size_t visit_count = 0;
    buffer.VisitData([&](uint8_t* data, uint32_t len) {
        EXPECT_EQ(payload.size(), len);
        EXPECT_EQ(0, std::memcmp(data, payload.data(), len));
        visit_count++;
    });
    EXPECT_EQ(1u, visit_count);

    std::array<uint8_t, 10> out{};
    EXPECT_EQ(payload.size(), buffer.Read(out.data(), out.size()));
    EXPECT_TRUE(std::equal(payload.begin(), payload.end(), out.begin()));
    EXPECT_EQ(0u, buffer.GetDataLength());
    buffer.Clear();  // should be a no-op when empty
}

TEST(SingleBlockBufferTest, MoveWritePointerAndClear) {
    auto chunk = MakeChunk(16);
    SingleBlockBuffer buffer(chunk);

    // Reserve bytes without writing data.
    EXPECT_EQ(5u, buffer.MoveWritePt(5));
    EXPECT_EQ(5u, buffer.GetDataLength());

    // Shrink the writable section.
    EXPECT_EQ(3u, buffer.MoveWritePt(-3));
    EXPECT_EQ(2u, buffer.GetDataLength());

    // Writable span matches the current write pointer.
    auto writable = buffer.GetWritableSpan();
    EXPECT_EQ(14u, writable.GetLength());  // 16 total - 2 consumed

    buffer.Clear();
    EXPECT_EQ(0u, buffer.GetDataLength());
    EXPECT_EQ(chunk->GetLength(), buffer.GetWritableSpan().GetLength());
}

TEST(SingleBlockBufferTest, InvalidBufferOperationsAreNoops) {
    SingleBlockBuffer buffer;  // no chunk attached

    std::array<uint8_t, 4> payload = {1, 2, 3, 4};
    EXPECT_EQ(0u, buffer.Write(payload.data(), payload.size()));
    EXPECT_EQ(0u, buffer.GetDataLength());

    size_t visits = 0;
    buffer.VisitData([&](uint8_t*, uint32_t) { visits++; });
    EXPECT_EQ(0u, visits);

    EXPECT_EQ(0u, buffer.MoveWritePt(3));
    EXPECT_EQ(0u, buffer.MoveReadPt(2));
    buffer.Clear();
}

TEST(SingleBlockBufferTest, VisitNoOpWhenEmpty) {
    auto chunk = MakeChunk(16);
    SingleBlockBuffer buffer(chunk);
    size_t visit_count = 0;
    buffer.VisitData([&](uint8_t*, uint32_t) { visit_count++; });
    EXPECT_EQ(0u, visit_count);
}

TEST(SingleBlockBufferTest, MoveFromAndMoveAssign) {
    auto chunk = MakeChunk(24);
    SingleBlockBuffer buffer(chunk);
    std::array<uint8_t, 4> payload = {9, 8, 7, 6};
    buffer.Write(payload.data(), payload.size());

    SingleBlockBuffer moved(std::move(buffer));
    EXPECT_FALSE(buffer.Valid());
    EXPECT_EQ(payload.size(), moved.GetDataLength());

    auto otherChunk = MakeChunk(24);
    SingleBlockBuffer other(otherChunk);
    other = std::move(moved);
    EXPECT_FALSE(moved.Valid());
    EXPECT_EQ(payload.size(), other.GetDataLength());
}

// ============================================================================
// Comprehensive tests for all methods and edge cases
// ============================================================================

// Test: Constructor and Valid()
TEST(SingleBlockBufferTest, ConstructorAndValid) {
    // Default constructor - no chunk
    SingleBlockBuffer buffer1;
    EXPECT_FALSE(buffer1.Valid());
    
    // Constructor with BufferChunk
    auto chunk1 = MakeChunk(32);
    SingleBlockBuffer buffer2(chunk1);
    EXPECT_TRUE(buffer2.Valid());
    
    // Constructor with StandaloneBufferChunk
    auto chunk2 = MakeStandaloneChunk(64);
    SingleBlockBuffer buffer3(chunk2);
    EXPECT_TRUE(buffer3.Valid());
    
    // Constructor with nullptr
    SingleBlockBuffer buffer4(nullptr);
    EXPECT_FALSE(buffer4.Valid());
}

// Test: Reset()
TEST(SingleBlockBufferTest, Reset) {
    SingleBlockBuffer buffer;
    EXPECT_FALSE(buffer.Valid());
    
    // Reset with valid chunk
    auto chunk1 = MakeChunk(32);
    buffer.Reset(chunk1);
    EXPECT_TRUE(buffer.Valid());
    EXPECT_EQ(0u, buffer.GetDataLength());
    EXPECT_EQ(32u, buffer.GetFreeLength());
    
    // Write some data
    std::array<uint8_t, 4> payload = {1, 2, 3, 4};
    buffer.Write(payload.data(), payload.size());
    EXPECT_EQ(4u, buffer.GetDataLength());
    
    // Reset with another chunk - should clear previous data
    auto chunk2 = MakeChunk(64);
    buffer.Reset(chunk2);
    EXPECT_TRUE(buffer.Valid());
    EXPECT_EQ(0u, buffer.GetDataLength());
    EXPECT_EQ(64u, buffer.GetFreeLength());
    
    // Reset with nullptr
    buffer.Reset(nullptr);
    EXPECT_FALSE(buffer.Valid());
}

// Test: Write() basic operations
TEST(SingleBlockBufferTest, WriteBasic) {
    auto chunk = MakeChunk(64);
    SingleBlockBuffer buffer(chunk);
    
    // Write small data
    std::array<uint8_t, 4> data1 = {1, 2, 3, 4};
    EXPECT_EQ(4u, buffer.Write(data1.data(), data1.size()));
    EXPECT_EQ(4u, buffer.GetDataLength());
    EXPECT_EQ(60u, buffer.GetFreeLength());
    
    // Write more data
    std::array<uint8_t, 8> data2 = {5, 6, 7, 8, 9, 10, 11, 12};
    EXPECT_EQ(8u, buffer.Write(data2.data(), data2.size()));
    EXPECT_EQ(12u, buffer.GetDataLength());
    EXPECT_EQ(52u, buffer.GetFreeLength());
}

// Test: Write() with nullptr
TEST(SingleBlockBufferTest, WriteNullptr) {
    auto chunk = MakeChunk(32);
    SingleBlockBuffer buffer(chunk);
    
    EXPECT_EQ(0u, buffer.Write(nullptr, 10));
    EXPECT_EQ(0u, buffer.GetDataLength());
}

// Test: Write() exceeding capacity
TEST(SingleBlockBufferTest, WriteExceedingCapacity) {
    auto chunk = MakeChunk(16);
    SingleBlockBuffer buffer(chunk);
    
    std::array<uint8_t, 32> large_data;
    std::fill(large_data.begin(), large_data.end(), 0xAA);
    
    // Should only write 16 bytes (buffer capacity)
    EXPECT_EQ(16u, buffer.Write(large_data.data(), large_data.size()));
    EXPECT_EQ(16u, buffer.GetDataLength());
    EXPECT_EQ(0u, buffer.GetFreeLength());
    
    // Try to write more - should fail
    std::array<uint8_t, 4> more_data = {1, 2, 3, 4};
    EXPECT_EQ(0u, buffer.Write(more_data.data(), more_data.size()));
}

// Test: Write() from another buffer
TEST(SingleBlockBufferTest, WriteFromBuffer) {
    auto chunk1 = MakeChunk(32);
    auto chunk2 = MakeChunk(64);
    
    SingleBlockBuffer src(chunk1);
    SingleBlockBuffer dst(chunk2);
    
    // Write data to source
    std::array<uint8_t, 8> data = {10, 20, 30, 40, 50, 60, 70, 80};
    src.Write(data.data(), data.size());
    
    // Write from source to destination
    EXPECT_EQ(8u, dst.Write(std::make_shared<SingleBlockBuffer>(std::move(src))));
    EXPECT_EQ(8u, dst.GetDataLength());
    
    // Verify data
    std::array<uint8_t, 8> out;
    dst.Read(out.data(), out.size());
    EXPECT_TRUE(std::equal(data.begin(), data.end(), out.begin()));
}

// Test: Write() from SharedBufferSpan
TEST(SingleBlockBufferTest, WriteFromSharedBufferSpan) {
    auto chunk1 = MakeChunk(32);
    auto chunk2 = MakeChunk(64);
    
    SingleBlockBuffer src(chunk1);
    SingleBlockBuffer dst(chunk2);
    
    // Write data to source
    std::array<uint8_t, 6> data = {11, 22, 33, 44, 55, 66};
    src.Write(data.data(), data.size());
    
    // Get span and write to destination
    auto span = src.GetSharedReadableSpan();
    EXPECT_EQ(6u, dst.Write(span));
    EXPECT_EQ(6u, dst.GetDataLength());
    
    // Verify data
    std::array<uint8_t, 6> out;
    dst.Read(out.data(), out.size());
    EXPECT_TRUE(std::equal(data.begin(), data.end(), out.begin()));
}

// Test: Write() from SharedBufferSpan with data_len
TEST(SingleBlockBufferTest, WriteFromSharedBufferSpanWithLength) {
    auto chunk1 = MakeChunk(32);
    auto chunk2 = MakeChunk(64);
    
    SingleBlockBuffer src(chunk1);
    SingleBlockBuffer dst(chunk2);
    
    // Write 10 bytes to source
    std::array<uint8_t, 10> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    src.Write(data.data(), data.size());
    
    // Get span and write only first 5 bytes to destination
    auto span = src.GetSharedReadableSpan();
    EXPECT_EQ(5u, dst.Write(span, 5));
    EXPECT_EQ(5u, dst.GetDataLength());
    
    // Verify only first 5 bytes were written
    std::array<uint8_t, 5> out;
    dst.Read(out.data(), out.size());
    EXPECT_TRUE(std::equal(data.begin(), data.begin() + 5, out.begin()));
}

// Test: Read() basic operations
TEST(SingleBlockBufferTest, ReadBasic) {
    auto chunk = MakeChunk(64);
    SingleBlockBuffer buffer(chunk);
    
    // Write data
    std::array<uint8_t, 12> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    buffer.Write(data.data(), data.size());
    
    // Read partial data
    std::array<uint8_t, 5> out1;
    EXPECT_EQ(5u, buffer.Read(out1.data(), out1.size()));
    EXPECT_TRUE(std::equal(data.begin(), data.begin() + 5, out1.begin()));
    EXPECT_EQ(7u, buffer.GetDataLength());
    
    // Read remaining data
    std::array<uint8_t, 10> out2;
    EXPECT_EQ(7u, buffer.Read(out2.data(), out2.size()));
    EXPECT_TRUE(std::equal(data.begin() + 5, data.end(), out2.begin()));
    EXPECT_EQ(0u, buffer.GetDataLength());
}

// Test: Read() with nullptr
TEST(SingleBlockBufferTest, ReadNullptr) {
    auto chunk = MakeChunk(32);
    SingleBlockBuffer buffer(chunk);
    
    std::array<uint8_t, 4> data = {1, 2, 3, 4};
    buffer.Write(data.data(), data.size());
    
    EXPECT_EQ(0u, buffer.Read(nullptr, 10));
    EXPECT_EQ(4u, buffer.GetDataLength());  // Data should still be there
}

// Test: Read() auto-clear when all data is read
TEST(SingleBlockBufferTest, ReadAutoClear) {
    auto chunk = MakeChunk(32);
    SingleBlockBuffer buffer(chunk);
    
    std::array<uint8_t, 8> data = {1, 2, 3, 4, 5, 6, 7, 8};
    buffer.Write(data.data(), data.size());
    
    // Read all data - should auto-clear
    std::array<uint8_t, 10> out;
    EXPECT_EQ(8u, buffer.Read(out.data(), out.size()));
    EXPECT_EQ(0u, buffer.GetDataLength());
    EXPECT_EQ(32u, buffer.GetFreeLength());  // Full capacity available again
}

// Test: ReadNotMovePt()
TEST(SingleBlockBufferTest, ReadNotMovePt) {
    auto chunk = MakeChunk(32);
    SingleBlockBuffer buffer(chunk);
    
    std::array<uint8_t, 6> data = {10, 20, 30, 40, 50, 60};
    buffer.Write(data.data(), data.size());
    
    // Read without moving pointer
    std::array<uint8_t, 4> out1;
    EXPECT_EQ(4u, buffer.ReadNotMovePt(out1.data(), out1.size()));
    EXPECT_TRUE(std::equal(data.begin(), data.begin() + 4, out1.begin()));
    EXPECT_EQ(6u, buffer.GetDataLength());  // Data length unchanged
    
    // Read again - should get same data
    std::array<uint8_t, 4> out2;
    EXPECT_EQ(4u, buffer.ReadNotMovePt(out2.data(), out2.size()));
    EXPECT_TRUE(std::equal(out1.begin(), out1.end(), out2.begin()));
    
    // Now actually read
    std::array<uint8_t, 6> out3;
    EXPECT_EQ(6u, buffer.Read(out3.data(), out3.size()));
    EXPECT_TRUE(std::equal(data.begin(), data.end(), out3.begin()));
}

// Test: ReadNotMovePt() with nullptr
TEST(SingleBlockBufferTest, ReadNotMovePtNullptr) {
    auto chunk = MakeChunk(32);
    SingleBlockBuffer buffer(chunk);
    
    std::array<uint8_t, 4> data = {1, 2, 3, 4};
    buffer.Write(data.data(), data.size());
    
    EXPECT_EQ(0u, buffer.ReadNotMovePt(nullptr, 10));
    EXPECT_EQ(4u, buffer.GetDataLength());
}

// Test: MoveReadPt() forward
TEST(SingleBlockBufferTest, MoveReadPtForward) {
    auto chunk = MakeChunk(32);
    SingleBlockBuffer buffer(chunk);
    
    std::array<uint8_t, 12> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    buffer.Write(data.data(), data.size());
    
    // Move forward 5 bytes
    EXPECT_EQ(5u, buffer.MoveReadPt(5));
    EXPECT_EQ(7u, buffer.GetDataLength());
    
    // Read remaining data
    std::array<uint8_t, 7> out;
    buffer.Read(out.data(), out.size());
    EXPECT_TRUE(std::equal(data.begin() + 5, data.end(), out.begin()));
}

// Test: MoveReadPt() forward beyond available data (auto-clear)
TEST(SingleBlockBufferTest, MoveReadPtForwardBeyond) {
    auto chunk = MakeChunk(32);
    SingleBlockBuffer buffer(chunk);
    
    std::array<uint8_t, 8> data = {1, 2, 3, 4, 5, 6, 7, 8};
    buffer.Write(data.data(), data.size());
    
    // Try to move 20 bytes forward (more than available)
    EXPECT_EQ(8u, buffer.MoveReadPt(20));
    EXPECT_EQ(0u, buffer.GetDataLength());
    EXPECT_EQ(32u, buffer.GetFreeLength());  // Auto-cleared
}

// Test: MoveReadPt() backward
TEST(SingleBlockBufferTest, MoveReadPtBackward) {
    auto chunk = MakeChunk(32);
    SingleBlockBuffer buffer(chunk);
    
    std::array<uint8_t, 10> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    buffer.Write(data.data(), data.size());
    
    // Read 6 bytes
    std::array<uint8_t, 6> out1;
    buffer.Read(out1.data(), out1.size());
    EXPECT_EQ(4u, buffer.GetDataLength());
    
    // Move backward 3 bytes
    EXPECT_EQ(3u, buffer.MoveReadPt(-3));
    EXPECT_EQ(7u, buffer.GetDataLength());
    
    // Read again - should get bytes 4-10
    std::array<uint8_t, 7> out2;
    buffer.Read(out2.data(), out2.size());
    EXPECT_TRUE(std::equal(data.begin() + 3, data.end(), out2.begin()));
}

// Test: MoveReadPt() backward beyond start
TEST(SingleBlockBufferTest, MoveReadPtBackwardBeyond) {
    auto chunk = MakeChunk(32);
    SingleBlockBuffer buffer(chunk);
    
    std::array<uint8_t, 10> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    buffer.Write(data.data(), data.size());
    
    // Read 5 bytes
    std::array<uint8_t, 5> out;
    buffer.Read(out.data(), out.size());
    
    // Try to move backward 10 bytes (more than read)
    EXPECT_EQ(5u, buffer.MoveReadPt(-10));
    EXPECT_EQ(10u, buffer.GetDataLength());  // Back to start
}

// Test: MoveWritePt() forward
TEST(SingleBlockBufferTest, MoveWritePtForward) {
    auto chunk = MakeChunk(32);
    SingleBlockBuffer buffer(chunk);
    
    // Reserve 10 bytes
    EXPECT_EQ(10u, buffer.MoveWritePt(10));
    EXPECT_EQ(10u, buffer.GetDataLength());
    EXPECT_EQ(22u, buffer.GetFreeLength());
    
    // Reserve 5 more bytes
    EXPECT_EQ(5u, buffer.MoveWritePt(5));
    EXPECT_EQ(15u, buffer.GetDataLength());
    EXPECT_EQ(17u, buffer.GetFreeLength());
}

// Test: MoveWritePt() forward beyond capacity
TEST(SingleBlockBufferTest, MoveWritePtForwardBeyond) {
    auto chunk = MakeChunk(16);
    SingleBlockBuffer buffer(chunk);
    
    // Try to reserve 100 bytes (more than capacity)
    EXPECT_EQ(16u, buffer.MoveWritePt(100));
    EXPECT_EQ(16u, buffer.GetDataLength());
    EXPECT_EQ(0u, buffer.GetFreeLength());
}

// Test: MoveWritePt() backward
TEST(SingleBlockBufferTest, MoveWritePtBackward) {
    auto chunk = MakeChunk(32);
    SingleBlockBuffer buffer(chunk);
    
    // Write some data
    std::array<uint8_t, 10> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    buffer.Write(data.data(), data.size());
    EXPECT_EQ(10u, buffer.GetDataLength());
    
    // Move write pointer back 4 bytes
    EXPECT_EQ(4u, buffer.MoveWritePt(-4));
    EXPECT_EQ(6u, buffer.GetDataLength());
    EXPECT_EQ(26u, buffer.GetFreeLength());
    
    // Read data - should only get first 6 bytes
    std::array<uint8_t, 10> out;
    EXPECT_EQ(6u, buffer.Read(out.data(), out.size()));
    EXPECT_TRUE(std::equal(data.begin(), data.begin() + 6, out.begin()));
}

// Test: MoveWritePt() backward beyond read pointer (auto-clear)
TEST(SingleBlockBufferTest, MoveWritePtBackwardBeyond) {
    auto chunk = MakeChunk(32);
    SingleBlockBuffer buffer(chunk);
    
    // Write 10 bytes
    std::array<uint8_t, 10> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    buffer.Write(data.data(), data.size());
    
    // Try to move backward 20 bytes (more than written)
    EXPECT_EQ(10u, buffer.MoveWritePt(-20));
    EXPECT_EQ(0u, buffer.GetDataLength());
    EXPECT_EQ(32u, buffer.GetFreeLength());  // Auto-cleared
}

// Test: GetData()
TEST(SingleBlockBufferTest, GetData) {
    auto chunk = MakeChunk(32);
    SingleBlockBuffer buffer(chunk);
    
    std::array<uint8_t, 6> data = {11, 22, 33, 44, 55, 66};
    buffer.Write(data.data(), data.size());
    
    uint8_t* ptr = buffer.GetData();
    ASSERT_NE(nullptr, ptr);
    EXPECT_EQ(0, std::memcmp(ptr, data.data(), data.size()));
}

// Test: GetDataAsString()
TEST(SingleBlockBufferTest, GetDataAsString) {
    auto chunk = MakeChunk(32);
    SingleBlockBuffer buffer(chunk);
    
    std::string text = "Hello, World!";
    buffer.Write(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    
    std::string result = buffer.GetDataAsString();
    EXPECT_EQ(text, result);
}

// Test: GetReadView()
TEST(SingleBlockBufferTest, GetReadView) {
    auto chunk = MakeChunk(32);
    SingleBlockBuffer buffer(chunk);
    
    std::array<uint8_t, 8> data = {1, 2, 3, 4, 5, 6, 7, 8};
    buffer.Write(data.data(), data.size());
    
    auto view = buffer.GetReadView();
    EXPECT_TRUE(view.Valid());
    EXPECT_EQ(8u, view.GetDataLength());
}

// Test: GetReadableSpan()
TEST(SingleBlockBufferTest, GetReadableSpan) {
    auto chunk = MakeChunk(32);
    SingleBlockBuffer buffer(chunk);
    
    std::array<uint8_t, 5> data = {10, 20, 30, 40, 50};
    buffer.Write(data.data(), data.size());
    
    auto span = buffer.GetReadableSpan();
    EXPECT_EQ(5u, span.GetLength());
    EXPECT_EQ(0, std::memcmp(span.GetStart(), data.data(), data.size()));
}

// Test: GetSharedReadableSpan() variants
TEST(SingleBlockBufferTest, GetSharedReadableSpan) {
    auto chunk = MakeChunk(32);
    SingleBlockBuffer buffer(chunk);
    
    std::array<uint8_t, 12> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    buffer.Write(data.data(), data.size());
    
    // Get all data
    auto span1 = buffer.GetSharedReadableSpan();
    EXPECT_TRUE(span1.Valid());
    EXPECT_EQ(12u, span1.GetLength());
    
    // Get specific length
    auto span2 = buffer.GetSharedReadableSpan(5);
    EXPECT_TRUE(span2.Valid());
    EXPECT_EQ(5u, span2.GetLength());
    
    // Get with must_fill_length = true
    auto span3 = buffer.GetSharedReadableSpan(10, true);
    EXPECT_TRUE(span3.Valid());
    EXPECT_EQ(10u, span3.GetLength());
    
    // Get with must_fill_length = true but insufficient data
    auto span4 = buffer.GetSharedReadableSpan(20, true);
    EXPECT_FALSE(span4.Valid());
    
    // Get with must_fill_length = false and insufficient data
    auto span5 = buffer.GetSharedReadableSpan(20, false);
    EXPECT_TRUE(span5.Valid());
    EXPECT_EQ(12u, span5.GetLength());  // Returns available data
}

// Test: GetWritableSpan() variants
TEST(SingleBlockBufferTest, GetWritableSpan) {
    auto chunk = MakeChunk(32);
    SingleBlockBuffer buffer(chunk);
    
    // Initially all space is writable
    auto span1 = buffer.GetWritableSpan();
    EXPECT_EQ(32u, span1.GetLength());
    
    // Write some data
    std::array<uint8_t, 10> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    buffer.Write(data.data(), data.size());
    
    // Writable space reduced
    auto span2 = buffer.GetWritableSpan();
    EXPECT_EQ(22u, span2.GetLength());
    
    // Get writable span with expected length
    auto span3 = buffer.GetWritableSpan(15);
    EXPECT_EQ(15u, span3.GetLength());
    
    // Get writable span with expected length exceeding available
    auto span4 = buffer.GetWritableSpan(30);
    EXPECT_EQ(0u, span4.GetLength());  // Insufficient space
}

// Test: GetFreeLength()
TEST(SingleBlockBufferTest, GetFreeLength) {
    auto chunk = MakeChunk(64);
    SingleBlockBuffer buffer(chunk);
    
    EXPECT_EQ(64u, buffer.GetFreeLength());
    
    buffer.MoveWritePt(20);
    EXPECT_EQ(44u, buffer.GetFreeLength());
    
    buffer.MoveWritePt(30);
    EXPECT_EQ(14u, buffer.GetFreeLength());
    
    buffer.MoveWritePt(14);
    EXPECT_EQ(0u, buffer.GetFreeLength());
}

// Test: Clear()
TEST(SingleBlockBufferTest, Clear) {
    auto chunk = MakeChunk(32);
    SingleBlockBuffer buffer(chunk);
    
    // Write data
    std::array<uint8_t, 10> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    buffer.Write(data.data(), data.size());
    EXPECT_EQ(10u, buffer.GetDataLength());
    EXPECT_EQ(22u, buffer.GetFreeLength());
    
    // Clear
    buffer.Clear();
    EXPECT_EQ(0u, buffer.GetDataLength());
    EXPECT_EQ(32u, buffer.GetFreeLength());
    
    // Can write again
    buffer.Write(data.data(), data.size());
    EXPECT_EQ(10u, buffer.GetDataLength());
}

// Test: VisitData()
TEST(SingleBlockBufferTest, VisitData) {
    auto chunk = MakeChunk(32);
    SingleBlockBuffer buffer(chunk);
    
    std::array<uint8_t, 8> data = {10, 20, 30, 40, 50, 60, 70, 80};
    buffer.Write(data.data(), data.size());
    
    size_t visit_count = 0;
    buffer.VisitData([&](uint8_t* ptr, uint32_t len) {
        EXPECT_EQ(8u, len);
        EXPECT_EQ(0, std::memcmp(ptr, data.data(), len));
        visit_count++;
    });
    EXPECT_EQ(1u, visit_count);
}

// Test: VisitData() with null visitor
TEST(SingleBlockBufferTest, VisitDataNullVisitor) {
    auto chunk = MakeChunk(32);
    SingleBlockBuffer buffer(chunk);
    
    std::array<uint8_t, 4> data = {1, 2, 3, 4};
    buffer.Write(data.data(), data.size());
    
    // Should not crash
    buffer.VisitData(nullptr);
}

// Test: VisitDataSpans()
TEST(SingleBlockBufferTest, VisitDataSpans) {
    auto chunk = MakeChunk(32);
    SingleBlockBuffer buffer(chunk);
    
    std::array<uint8_t, 6> data = {11, 22, 33, 44, 55, 66};
    buffer.Write(data.data(), data.size());
    
    size_t visit_count = 0;
    buffer.VisitDataSpans([&](SharedBufferSpan& span) {
        EXPECT_TRUE(span.Valid());
        EXPECT_EQ(6u, span.GetLength());
        EXPECT_EQ(0, std::memcmp(span.GetStart(), data.data(), span.GetLength()));
        visit_count++;
    });
    EXPECT_EQ(1u, visit_count);
}

// Test: VisitDataSpans() with null visitor
TEST(SingleBlockBufferTest, VisitDataSpansNullVisitor) {
    auto chunk = MakeChunk(32);
    SingleBlockBuffer buffer(chunk);
    
    std::array<uint8_t, 4> data = {1, 2, 3, 4};
    buffer.Write(data.data(), data.size());
    
    // Should not crash
    buffer.VisitDataSpans(nullptr);
}

// Test: GetChunk()
TEST(SingleBlockBufferTest, GetChunk) {
    auto chunk = MakeChunk(32);
    SingleBlockBuffer buffer(chunk);
    
    auto retrieved = buffer.GetChunk();
    EXPECT_EQ(chunk, retrieved);
}

// Test: ShallowClone()
TEST(SingleBlockBufferTest, ShallowClone) {
    auto chunk = MakeChunk(32);
    SingleBlockBuffer buffer(chunk);
    
    // Write some data
    std::array<uint8_t, 8> data = {1, 2, 3, 4, 5, 6, 7, 8};
    buffer.Write(data.data(), data.size());
    
    // Read 3 bytes
    std::array<uint8_t, 3> temp;
    buffer.Read(temp.data(), temp.size());
    EXPECT_EQ(5u, buffer.GetDataLength());
    
    // Clone
    auto cloned = buffer.ShallowClone();
    ASSERT_NE(nullptr, cloned);
    
    auto cloned_single = std::dynamic_pointer_cast<SingleBlockBuffer>(cloned);
    ASSERT_NE(nullptr, cloned_single);
    
    // Clone should have same state
    EXPECT_EQ(5u, cloned_single->GetDataLength());
    EXPECT_EQ(buffer.GetFreeLength(), cloned_single->GetFreeLength());
    
    // Clone should share the same chunk
    EXPECT_EQ(buffer.GetChunk(), cloned_single->GetChunk());
    
    // Verify cloned data
    std::array<uint8_t, 5> out;
    cloned_single->Read(out.data(), out.size());
    EXPECT_TRUE(std::equal(data.begin() + 3, data.end(), out.begin()));
}

// Test: Move constructor self-assignment
TEST(SingleBlockBufferTest, MoveSelfAssignment) {
    auto chunk = MakeChunk(32);
    SingleBlockBuffer buffer(chunk);
    
    std::array<uint8_t, 4> data = {1, 2, 3, 4};
    buffer.Write(data.data(), data.size());
    
    // Self-assignment should be safe
    buffer = std::move(buffer);
    EXPECT_TRUE(buffer.Valid());
    EXPECT_EQ(4u, buffer.GetDataLength());
}

// Test: Operations on invalid buffer
TEST(SingleBlockBufferTest, InvalidBufferOperations) {
    SingleBlockBuffer buffer;
    
    std::array<uint8_t, 4> data = {1, 2, 3, 4};
    std::array<uint8_t, 4> out;
    
    // All operations should fail gracefully
    EXPECT_EQ(0u, buffer.Write(data.data(), data.size()));
    EXPECT_EQ(0u, buffer.Read(out.data(), out.size()));
    EXPECT_EQ(0u, buffer.ReadNotMovePt(out.data(), out.size()));
    EXPECT_EQ(0u, buffer.MoveReadPt(5));
    EXPECT_EQ(0u, buffer.MoveWritePt(5));
    EXPECT_EQ(0u, buffer.GetDataLength());
    EXPECT_EQ(0u, buffer.GetFreeLength());
    EXPECT_EQ(nullptr, buffer.GetData());
    EXPECT_EQ("", buffer.GetDataAsString());
    
    auto view = buffer.GetReadView();
    EXPECT_FALSE(view.Valid());
    
    auto span = buffer.GetReadableSpan();
    EXPECT_EQ(0u, span.GetLength());
    
    auto shared_span = buffer.GetSharedReadableSpan();
    EXPECT_FALSE(shared_span.Valid());
    
    auto writable_span = buffer.GetWritableSpan();
    EXPECT_EQ(0u, writable_span.GetLength());
    
    buffer.Clear();  // Should not crash
}

// Test: Sequential read/write cycles
TEST(SingleBlockBufferTest, SequentialReadWriteCycles) {
    auto chunk = MakeChunk(64);
    SingleBlockBuffer buffer(chunk);
    
    // Cycle 1
    std::array<uint8_t, 10> data1 = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    buffer.Write(data1.data(), data1.size());
    std::array<uint8_t, 10> out1;
    buffer.Read(out1.data(), out1.size());
    EXPECT_TRUE(std::equal(data1.begin(), data1.end(), out1.begin()));
    
    // Cycle 2 - buffer should be auto-cleared and reusable
    std::array<uint8_t, 15> data2;
    std::fill(data2.begin(), data2.end(), 0xAA);
    buffer.Write(data2.data(), data2.size());
    EXPECT_EQ(15u, buffer.GetDataLength());
    std::array<uint8_t, 15> out2;
    buffer.Read(out2.data(), out2.size());
    EXPECT_TRUE(std::equal(data2.begin(), data2.end(), out2.begin()));
    
    // Cycle 3
    std::array<uint8_t, 20> data3;
    std::fill(data3.begin(), data3.end(), 0xBB);
    buffer.Write(data3.data(), data3.size());
    EXPECT_EQ(20u, buffer.GetDataLength());
}

// Test: Large data operations
TEST(SingleBlockBufferTest, LargeDataOperations) {
    auto chunk = MakeStandaloneChunk(4096);
    SingleBlockBuffer buffer(chunk);
    
    // Write large data
    std::vector<uint8_t> large_data(4096);
    for (size_t i = 0; i < large_data.size(); ++i) {
        large_data[i] = static_cast<uint8_t>(i % 256);
    }
    
    EXPECT_EQ(4096u, buffer.Write(large_data.data(), large_data.size()));
    EXPECT_EQ(4096u, buffer.GetDataLength());
    EXPECT_EQ(0u, buffer.GetFreeLength());
    
    // Read in chunks
    std::vector<uint8_t> read_data(4096);
    size_t offset = 0;
    while (offset < 4096) {
        uint32_t chunk_size = 512;
        uint32_t read = buffer.Read(read_data.data() + offset, chunk_size);
        offset += read;
        if (read == 0) break;
    }
    
    EXPECT_EQ(4096u, offset);
    EXPECT_TRUE(std::equal(large_data.begin(), large_data.end(), read_data.begin()));
}

// Test: Boundary conditions
TEST(SingleBlockBufferTest, BoundaryConditions) {
    auto chunk = MakeChunk(16);
    SingleBlockBuffer buffer(chunk);
    
    // Write exactly buffer size
    std::array<uint8_t, 16> data;
    std::fill(data.begin(), data.end(), 0x55);
    EXPECT_EQ(16u, buffer.Write(data.data(), data.size()));
    EXPECT_EQ(16u, buffer.GetDataLength());
    EXPECT_EQ(0u, buffer.GetFreeLength());
    
    // Read exactly buffer size
    std::array<uint8_t, 16> out;
    EXPECT_EQ(16u, buffer.Read(out.data(), out.size()));
    EXPECT_TRUE(std::equal(data.begin(), data.end(), out.begin()));
    EXPECT_EQ(0u, buffer.GetDataLength());
    EXPECT_EQ(16u, buffer.GetFreeLength());
}

// Test: Zero-length operations
TEST(SingleBlockBufferTest, ZeroLengthOperations) {
    auto chunk = MakeChunk(32);
    SingleBlockBuffer buffer(chunk);
    
    // Write zero bytes
    std::array<uint8_t, 4> data = {1, 2, 3, 4};
    EXPECT_EQ(0u, buffer.Write(data.data(), 0));
    EXPECT_EQ(0u, buffer.GetDataLength());
    
    // Write some data
    buffer.Write(data.data(), data.size());
    
    // Read zero bytes
    std::array<uint8_t, 4> out;
    EXPECT_EQ(0u, buffer.Read(out.data(), 0));
    EXPECT_EQ(4u, buffer.GetDataLength());  // Data unchanged
    
    // Move pointers by zero
    EXPECT_EQ(0u, buffer.MoveReadPt(0));
    EXPECT_EQ(0u, buffer.MoveWritePt(0));
}

}
}
}
