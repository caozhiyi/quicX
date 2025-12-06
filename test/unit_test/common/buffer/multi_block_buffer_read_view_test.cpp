#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_chunk.h"
#include "common/buffer/multi_block_buffer.h"
#include "common/buffer/multi_block_buffer_read_view.h"
#include "common/buffer/shared_buffer_span.h"
#include "common/buffer/single_block_buffer.h"

namespace quicx {
namespace common {
namespace {

// Helper function to create a pool
std::shared_ptr<BlockMemoryPool> MakePool(uint32_t size = 64, uint32_t count = 4) {
    return MakeBlockMemoryPoolPtr(size, count);
}

// ============================================================================
// Basic Construction and Validity Tests
// ============================================================================

TEST(MultiBlockBufferReadViewTest, DefaultConstruction) {
    MultiBlockBufferReadView view;
    EXPECT_FALSE(view.Valid());
    EXPECT_EQ(0u, view.GetDataLength());
    EXPECT_EQ(0u, view.GetReadOffset());
}

TEST(MultiBlockBufferReadViewTest, ConstructionWithBuffer) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);
    
    // Write some data
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));
    
    MultiBlockBufferReadView view(buffer);
    EXPECT_TRUE(view.Valid());
    EXPECT_EQ(5u, view.GetDataLength());
    EXPECT_EQ(0u, view.GetReadOffset());
}

TEST(MultiBlockBufferReadViewTest, Reset) {
    auto pool = MakePool(64, 4);
    auto buffer1 = std::make_shared<MultiBlockBuffer>(pool);
    auto buffer2 = std::make_shared<MultiBlockBuffer>(pool);
    
    buffer1->Write(reinterpret_cast<const uint8_t*>("hello"), 5);
    buffer2->Write(reinterpret_cast<const uint8_t*>("world"), 5);
    
    MultiBlockBufferReadView view(buffer1);
    EXPECT_EQ(5u, view.GetDataLength());
    
    view.Reset(buffer2);
    EXPECT_EQ(5u, view.GetDataLength());
    EXPECT_EQ(0u, view.GetReadOffset());
}

TEST(MultiBlockBufferReadViewTest, Clear) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);
    buffer->Write(reinterpret_cast<const uint8_t*>("test"), 4);
    
    MultiBlockBufferReadView view(buffer);
    EXPECT_TRUE(view.Valid());
    
    view.Clear();
    EXPECT_FALSE(view.Valid());
    EXPECT_EQ(0u, view.GetDataLength());
    EXPECT_EQ(0u, view.GetReadOffset());
}

// ============================================================================
// ReadNotMovePt Tests
// ============================================================================

TEST(MultiBlockBufferReadViewTest, ReadNotMovePtBasic) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);
    
    std::vector<uint8_t> data = {1, 2, 3, 4, 5, 6, 7, 8};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));
    
    MultiBlockBufferReadView view(buffer);
    
    uint8_t out[8] = {0};
    uint32_t read = view.ReadNotMovePt(out, 8);
    EXPECT_EQ(8u, read);
    EXPECT_EQ(0, memcmp(out, data.data(), 8));
    
    // Read again should get same data (pointer not moved)
    uint8_t out2[8] = {0};
    read = view.ReadNotMovePt(out2, 8);
    EXPECT_EQ(8u, read);
    EXPECT_EQ(0, memcmp(out2, data.data(), 8));
    EXPECT_EQ(0u, view.GetReadOffset());
}

TEST(MultiBlockBufferReadViewTest, ReadNotMovePtPartial) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);
    
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));
    
    MultiBlockBufferReadView view(buffer);
    
    uint8_t out[3] = {0};
    uint32_t read = view.ReadNotMovePt(out, 3);
    EXPECT_EQ(3u, read);
    EXPECT_EQ(1u, out[0]);
    EXPECT_EQ(2u, out[1]);
    EXPECT_EQ(3u, out[2]);
    EXPECT_EQ(0u, view.GetReadOffset());
}

TEST(MultiBlockBufferReadViewTest, ReadNotMovePtAfterMove) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);
    
    std::vector<uint8_t> data = {1, 2, 3, 4, 5, 6, 7, 8};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));
    
    MultiBlockBufferReadView view(buffer);
    
    // Move read pointer forward
    view.MoveReadPt(3);
    EXPECT_EQ(3u, view.GetReadOffset());
    
    // Read should start from offset 3
    uint8_t out[5] = {0};
    uint32_t read = view.ReadNotMovePt(out, 5);
    EXPECT_EQ(5u, read);
    EXPECT_EQ(4u, out[0]);  // data[3]
    EXPECT_EQ(5u, out[1]);  // data[4]
    EXPECT_EQ(6u, out[2]);  // data[5]
    EXPECT_EQ(7u, out[3]);  // data[6]
    EXPECT_EQ(8u, out[4]);  // data[7]
}

TEST(MultiBlockBufferReadViewTest, ReadNotMovePtEmptyBuffer) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);
    
    MultiBlockBufferReadView view(buffer);
    
    uint8_t out[10] = {0};
    uint32_t read = view.ReadNotMovePt(out, 10);
    EXPECT_EQ(0u, read);
}

TEST(MultiBlockBufferReadViewTest, ReadNotMovePtInvalidView) {
    MultiBlockBufferReadView view;
    
    uint8_t out[10] = {0};
    uint32_t read = view.ReadNotMovePt(out, 10);
    EXPECT_EQ(0u, read);
}

// ============================================================================
// Read Tests (moves pointer)
// ============================================================================

TEST(MultiBlockBufferReadViewTest, ReadBasic) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);
    
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));
    
    MultiBlockBufferReadView view(buffer);
    
    uint8_t out[5] = {0};
    uint32_t read = view.Read(out, 5);
    EXPECT_EQ(5u, read);
    EXPECT_EQ(0, memcmp(out, data.data(), 5));
    EXPECT_EQ(5u, view.GetReadOffset());
    EXPECT_EQ(0u, view.GetDataLength());
}

TEST(MultiBlockBufferReadViewTest, ReadPartial) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);
    
    std::vector<uint8_t> data = {1, 2, 3, 4, 5, 6, 7, 8};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));
    
    MultiBlockBufferReadView view(buffer);
    
    uint8_t out[3] = {0};
    uint32_t read = view.Read(out, 3);
    EXPECT_EQ(3u, read);
    EXPECT_EQ(3u, view.GetReadOffset());
    EXPECT_EQ(5u, view.GetDataLength());
    
    // Read more
    uint8_t out2[5] = {0};
    read = view.Read(out2, 5);
    EXPECT_EQ(5u, read);
    EXPECT_EQ(8u, view.GetReadOffset());
    EXPECT_EQ(0u, view.GetDataLength());
}

TEST(MultiBlockBufferReadViewTest, ReadMultipleTimes) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);
    
    std::vector<uint8_t> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));
    
    MultiBlockBufferReadView view(buffer);
    
    for (uint32_t i = 0; i < 10; ++i) {
        uint8_t out = 0;
        uint32_t read = view.Read(&out, 1);
        EXPECT_EQ(1u, read);
        EXPECT_EQ(static_cast<uint8_t>(i + 1), out);
        EXPECT_EQ(i + 1, view.GetReadOffset());
    }
    
    EXPECT_EQ(10u, view.GetReadOffset());
    EXPECT_EQ(0u, view.GetDataLength());
}

// ============================================================================
// MoveReadPt Tests
// ============================================================================

TEST(MultiBlockBufferReadViewTest, MoveReadPtBasic) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);
    
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));
    
    MultiBlockBufferReadView view(buffer);
    
    uint32_t moved = view.MoveReadPt(3);
    EXPECT_EQ(3u, moved);
    EXPECT_EQ(3u, view.GetReadOffset());
    EXPECT_EQ(2u, view.GetDataLength());
}

TEST(MultiBlockBufferReadViewTest, MoveReadPtBeyondEnd) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);
    
    std::vector<uint8_t> data = {1, 2, 3};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));
    
    MultiBlockBufferReadView view(buffer);
    
    uint32_t moved = view.MoveReadPt(10);
    EXPECT_EQ(3u, moved);  // Only 3 bytes available
    EXPECT_EQ(3u, view.GetReadOffset());
    EXPECT_EQ(0u, view.GetDataLength());
}

TEST(MultiBlockBufferReadViewTest, MoveReadPtZero) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);
    
    std::vector<uint8_t> data = {1, 2, 3};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));
    
    MultiBlockBufferReadView view(buffer);
    
    uint32_t moved = view.MoveReadPt(0);
    EXPECT_EQ(0u, moved);
    EXPECT_EQ(0u, view.GetReadOffset());
}

// ============================================================================
// Sync Tests
// ============================================================================

TEST(MultiBlockBufferReadViewTest, SyncBasic) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);
    
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));
    
    EXPECT_EQ(5u, buffer->GetDataLength());
    
    MultiBlockBufferReadView view(buffer);
    view.MoveReadPt(3);
    EXPECT_EQ(3u, view.GetReadOffset());
    EXPECT_EQ(5u, buffer->GetDataLength());  // Buffer not moved yet
    
    view.Sync();
    EXPECT_EQ(0u, view.GetReadOffset());
    EXPECT_EQ(2u, buffer->GetDataLength());  // Buffer moved forward
}

TEST(MultiBlockBufferReadViewTest, SyncNoOffset) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);
    
    std::vector<uint8_t> data = {1, 2, 3};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));
    
    MultiBlockBufferReadView view(buffer);
    EXPECT_EQ(0u, view.GetReadOffset());
    
    view.Sync();
    EXPECT_EQ(0u, view.GetReadOffset());
    EXPECT_EQ(3u, buffer->GetDataLength());  // No change
}

// ============================================================================
// VisitData Tests
// ============================================================================

TEST(MultiBlockBufferReadViewTest, VisitDataBasic) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);
    
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));
    
    MultiBlockBufferReadView view(buffer);
    
    std::vector<uint8_t> visited;
    view.VisitData([&](uint8_t* data, uint32_t len) {
        visited.insert(visited.end(), data, data + len);
        return true;
    });
    
    EXPECT_EQ(5u, visited.size());
    EXPECT_EQ(0, memcmp(visited.data(), data.data(), 5));
}

TEST(MultiBlockBufferReadViewTest, VisitDataAfterMove) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);
    
    std::vector<uint8_t> data = {1, 2, 3, 4, 5, 6, 7, 8};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));
    
    MultiBlockBufferReadView view(buffer);
    view.MoveReadPt(3);
    
    std::vector<uint8_t> visited;
    view.VisitData([&](uint8_t* data, uint32_t len) {
        visited.insert(visited.end(), data, data + len);
        return true;
    });
    
    EXPECT_EQ(5u, visited.size());
    EXPECT_EQ(4u, visited[0]);  // data[3]
    EXPECT_EQ(5u, visited[1]);  // data[4]
    EXPECT_EQ(6u, visited[2]);  // data[5]
    EXPECT_EQ(7u, visited[3]);  // data[6]
    EXPECT_EQ(8u, visited[4]);  // data[7]
}

TEST(MultiBlockBufferReadViewTest, VisitDataEmpty) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);
    
    MultiBlockBufferReadView view(buffer);
    
    bool called = false;
    view.VisitData([&](uint8_t* data, uint32_t len) {
        called = true;
        return true;
    });
    
    EXPECT_FALSE(called);
}

// ============================================================================
// Multi-Block Tests
// ============================================================================

TEST(MultiBlockBufferReadViewTest, ReadAcrossMultipleBlocks) {
    auto pool = MakePool(32, 4);  // Small chunk size to force multiple blocks
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);
    
    // Write data that will span multiple chunks
    std::vector<uint8_t> data1(20, 0xAA);
    std::vector<uint8_t> data2(20, 0xBB);
    
    buffer->Write(data1.data(), 20);
    buffer->Write(data2.data(), 20);
    
    EXPECT_EQ(40u, buffer->GetDataLength());
    
    MultiBlockBufferReadView view(buffer);
    
    // Read across block boundary
    uint8_t out[30] = {0};
    uint32_t read = view.ReadNotMovePt(out, 30);
    EXPECT_EQ(30u, read);
    
    // Verify first 20 bytes
    for (uint32_t i = 0; i < 20; ++i) {
        EXPECT_EQ(0xAA, out[i]);
    }
    // Verify next 10 bytes
    for (uint32_t i = 20; i < 30; ++i) {
        EXPECT_EQ(0xBB, out[i]);
    }
}

TEST(MultiBlockBufferReadViewTest, ReadLargeDataAcrossBlocks) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);
    
    // Write large data
    std::vector<uint8_t> large_data(200);
    for (size_t i = 0; i < large_data.size(); ++i) {
        large_data[i] = static_cast<uint8_t>(i % 256);
    }
    buffer->Write(large_data.data(), static_cast<uint32_t>(large_data.size()));
    
    MultiBlockBufferReadView view(buffer);
    
    // Read all data
    std::vector<uint8_t> out(200);
    uint32_t read = view.Read(out.data(), 200);
    EXPECT_EQ(200u, read);
    EXPECT_EQ(0, memcmp(out.data(), large_data.data(), 200));
    EXPECT_EQ(200u, view.GetReadOffset());
}

// ============================================================================
// Integration with SingleBlockBuffer
// ============================================================================

TEST(MultiBlockBufferReadViewTest, WithSingleBlockBuffer) {
    auto pool = MakePool(64, 4);
    auto chunk = std::make_shared<BufferChunk>(pool);
    auto buffer = std::make_shared<SingleBlockBuffer>(chunk);
    
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));
    
    MultiBlockBufferReadView view(buffer);
    
    uint8_t out[5] = {0};
    uint32_t read = view.Read(out, 5);
    EXPECT_EQ(5u, read);
    EXPECT_EQ(0, memcmp(out, data.data(), 5));
}

}  // namespace
}  // namespace common
}  // namespace quicx

