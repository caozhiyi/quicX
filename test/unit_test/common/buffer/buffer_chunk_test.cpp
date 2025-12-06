
#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_chunk.h"
#include "common/buffer/standalone_buffer_chunk.h"

namespace quicx {
namespace common {
namespace {

TEST(BufferChunkTest, InvalidWhenPoolNullptr) {
    BufferChunk chunk(nullptr);
    EXPECT_FALSE(chunk.Valid());
    EXPECT_EQ(nullptr, chunk.GetData());
    EXPECT_EQ(0u, chunk.GetLength());
}

TEST(BufferChunkTest, AllocationAndRelease) {
    auto pool = MakeBlockMemoryPoolPtr(/*large_sz=*/64u, /*add_num=*/1u);
    ASSERT_NE(nullptr, pool);
    EXPECT_EQ(0u, pool->GetSize());

    {
        BufferChunk chunk(pool);
        ASSERT_TRUE(chunk.Valid());
        EXPECT_NE(nullptr, chunk.GetData());
        EXPECT_EQ(64u, chunk.GetLength());
        // Pool should have expanded once (one block taken).
        EXPECT_EQ(0u, pool->GetSize());
    }

    // After chunk is destroyed the memory should be returned to the pool.
    EXPECT_EQ(1u, pool->GetSize());
}

TEST(BufferChunkTest, MoveTransfersOwnership) {
    auto pool = MakeBlockMemoryPoolPtr(/*large_sz=*/32u, /*add_num=*/1u);
    BufferChunk first(pool);
    ASSERT_TRUE(first.Valid());
    auto* data = first.GetData();

    BufferChunk second(std::move(first));
    EXPECT_FALSE(first.Valid());
    ASSERT_TRUE(second.Valid());
    EXPECT_EQ(data, second.GetData());
    EXPECT_EQ(32u, second.GetLength());

    BufferChunk third(pool);
    // Move assignment should release second's memory first.
    auto* thirdData = third.GetData();
    second = std::move(third);
    EXPECT_FALSE(third.Valid());
    ASSERT_TRUE(second.Valid());
    EXPECT_EQ(thirdData, second.GetData());
}

TEST(StandaloneBufferChunkTest, AllocatesAndReleases) {
    StandaloneBufferChunk chunk(128u);
    ASSERT_TRUE(chunk.Valid());
    EXPECT_NE(nullptr, chunk.GetData());
    EXPECT_EQ(128u, chunk.GetLength());

    StandaloneBufferChunk moved(std::move(chunk));
    EXPECT_FALSE(chunk.Valid());
    EXPECT_TRUE(moved.Valid());
    EXPECT_EQ(128u, moved.GetLength());
}

// ============================================================================
// Extended BufferChunk tests
// ============================================================================

TEST(BufferChunkTest, GetPool) {
    auto pool = MakeBlockMemoryPoolPtr(64u, 1u);
    BufferChunk chunk(pool);
    
    ASSERT_TRUE(chunk.Valid());
    EXPECT_EQ(pool, chunk.GetPool());
}

TEST(BufferChunkTest, SelfMoveAssignment) {
    auto pool = MakeBlockMemoryPoolPtr(32u, 1u);
    BufferChunk chunk(pool);
    ASSERT_TRUE(chunk.Valid());
    auto* data = chunk.GetData();
    
    // Self-assignment should be safe
    chunk = std::move(chunk);
    EXPECT_TRUE(chunk.Valid());
    EXPECT_EQ(data, chunk.GetData());
}

TEST(BufferChunkTest, MultipleChunksFromSamePool) {
    auto pool = MakeBlockMemoryPoolPtr(64u, 3u);
    
    BufferChunk chunk1(pool);
    BufferChunk chunk2(pool);
    BufferChunk chunk3(pool);
    
    EXPECT_TRUE(chunk1.Valid());
    EXPECT_TRUE(chunk2.Valid());
    EXPECT_TRUE(chunk3.Valid());
    
    // All should have different data pointers
    EXPECT_NE(chunk1.GetData(), chunk2.GetData());
    EXPECT_NE(chunk2.GetData(), chunk3.GetData());
    EXPECT_NE(chunk1.GetData(), chunk3.GetData());
}

TEST(BufferChunkTest, PoolRecycling) {
    auto pool = MakeBlockMemoryPoolPtr(64u, 1u);
    EXPECT_EQ(0u, pool->GetSize());
    
    uint8_t* first_data = nullptr;
    {
        BufferChunk chunk1(pool);
        first_data = chunk1.GetData();
        EXPECT_EQ(0u, pool->GetSize());
    }
    EXPECT_EQ(1u, pool->GetSize());
    
    {
        BufferChunk chunk2(pool);
        // Should reuse the same memory
        EXPECT_EQ(first_data, chunk2.GetData());
        EXPECT_EQ(0u, pool->GetSize());
    }
    EXPECT_EQ(1u, pool->GetSize());
}

TEST(BufferChunkTest, WriteAndReadData) {
    auto pool = MakeBlockMemoryPoolPtr(64u, 1u);
    BufferChunk chunk(pool);
    
    ASSERT_TRUE(chunk.Valid());
    auto* data = chunk.GetData();
    
    // Write data
    for (uint32_t i = 0; i < 10; ++i) {
        data[i] = static_cast<uint8_t>(i + 1);
    }
    
    // Read and verify
    for (uint32_t i = 0; i < 10; ++i) {
        EXPECT_EQ(i + 1, data[i]);
    }
}

TEST(BufferChunkTest, SetLimitSize) {
    auto pool = MakeBlockMemoryPoolPtr(64u, 1u);
    BufferChunk chunk(pool);
    
    ASSERT_TRUE(chunk.Valid());
    EXPECT_EQ(64u, chunk.GetLength());
    
    // Set limit size
    chunk.SetLimitSize(32);
    EXPECT_EQ(32u, chunk.GetLength());
    
    // Set limit size larger than actual size
    chunk.SetLimitSize(100);
    EXPECT_EQ(64u, chunk.GetLength());  // Should be clamped
}

// ============================================================================
// Extended StandaloneBufferChunk tests
// ============================================================================

TEST(StandaloneBufferChunkTest, ZeroSizeAllocation) {
    StandaloneBufferChunk chunk(0);
    // Implementation-defined: might be valid with nullptr or invalid
    if (chunk.Valid()) {
        EXPECT_EQ(0u, chunk.GetLength());
    }
}

TEST(StandaloneBufferChunkTest, LargeAllocation) {
    StandaloneBufferChunk chunk(4096);
    ASSERT_TRUE(chunk.Valid());
    EXPECT_NE(nullptr, chunk.GetData());
    EXPECT_EQ(4096u, chunk.GetLength());
}

TEST(StandaloneBufferChunkTest, MoveAssignment) {
    StandaloneBufferChunk chunk1(128);
    StandaloneBufferChunk chunk2(64);
    
    ASSERT_TRUE(chunk1.Valid());
    ASSERT_TRUE(chunk2.Valid());
    auto* data1 = chunk1.GetData();
    
    chunk2 = std::move(chunk1);
    EXPECT_FALSE(chunk1.Valid());
    EXPECT_TRUE(chunk2.Valid());
    EXPECT_EQ(data1, chunk2.GetData());
    EXPECT_EQ(128u, chunk2.GetLength());
}

TEST(StandaloneBufferChunkTest, SelfMoveAssignment) {
    StandaloneBufferChunk chunk(64);
    ASSERT_TRUE(chunk.Valid());
    auto* data = chunk.GetData();
    
    chunk = std::move(chunk);
    EXPECT_TRUE(chunk.Valid());
    EXPECT_EQ(data, chunk.GetData());
}

TEST(StandaloneBufferChunkTest, GetPoolReturnsNull) {
    StandaloneBufferChunk chunk(64);
    EXPECT_EQ(nullptr, chunk.GetPool());
}

TEST(StandaloneBufferChunkTest, WriteAndReadData) {
    StandaloneBufferChunk chunk(64);
    ASSERT_TRUE(chunk.Valid());
    auto* data = chunk.GetData();
    
    // Write pattern
    for (uint32_t i = 0; i < chunk.GetLength(); ++i) {
        data[i] = static_cast<uint8_t>(i % 256);
    }
    
    // Verify pattern
    for (uint32_t i = 0; i < chunk.GetLength(); ++i) {
        EXPECT_EQ(static_cast<uint8_t>(i % 256), data[i]);
    }
}

TEST(StandaloneBufferChunkTest, MultipleIndependentChunks) {
    StandaloneBufferChunk chunk1(64);
    StandaloneBufferChunk chunk2(128);
    StandaloneBufferChunk chunk3(256);
    
    EXPECT_TRUE(chunk1.Valid());
    EXPECT_TRUE(chunk2.Valid());
    EXPECT_TRUE(chunk3.Valid());
    
    EXPECT_EQ(64u, chunk1.GetLength());
    EXPECT_EQ(128u, chunk2.GetLength());
    EXPECT_EQ(256u, chunk3.GetLength());
    
    // All should have different data pointers
    EXPECT_NE(chunk1.GetData(), chunk2.GetData());
    EXPECT_NE(chunk2.GetData(), chunk3.GetData());
    EXPECT_NE(chunk1.GetData(), chunk3.GetData());
}

}
}
}
