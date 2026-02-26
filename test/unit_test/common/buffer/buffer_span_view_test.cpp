#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_chunk.h"
#include "common/buffer/buffer_span.h"
#include "common/buffer/shared_buffer_span.h"

namespace quicx {
namespace common {
namespace {

TEST(BufferSpanTest, ValidRange) {
    uint8_t storage[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    BufferSpan span(storage, storage + 8);
    ASSERT_TRUE(span.Valid());
    EXPECT_EQ(storage, span.GetStart());
    EXPECT_EQ(storage + 8, span.GetEnd());
    EXPECT_EQ(8u, span.GetLength());
}

TEST(BufferSpanTest, InvalidRangeResetsPointers) {
    BufferSpan span(nullptr, nullptr);
    EXPECT_FALSE(span.Valid());

    uint8_t storage[4];
    BufferSpan reversed(storage + 4, storage);
    EXPECT_FALSE(reversed.Valid());
}

TEST(BufferSpanTest, LengthConstructor) {
    uint8_t storage[5] = {0};
    BufferSpan span(storage, 3u);
    ASSERT_TRUE(span.Valid());
    EXPECT_EQ(storage + 3, span.GetEnd());
    EXPECT_EQ(3u, span.GetLength());
}

TEST(SharedBufferSpanTest, KeepsChunkAlive) {
    auto pool = MakeBlockMemoryPoolPtr(/*large_sz=*/32u, /*add_num=*/1u);
    auto chunk = std::make_shared<BufferChunk>(pool);
    ASSERT_TRUE(chunk->Valid());

    SharedBufferSpan span(chunk, chunk->GetData(), 16u);
    ASSERT_TRUE(span.Valid());
    EXPECT_EQ(16u, span.GetLength());
    EXPECT_EQ(chunk, span.GetChunk());

    chunk.reset();
    EXPECT_TRUE(span.Valid());
    EXPECT_NE(nullptr, span.GetStart());
    EXPECT_EQ(16u, span.GetLength());
}

TEST(SharedBufferSpanTest, InvalidConstruction) {
    auto pool = MakeBlockMemoryPoolPtr(/*large_sz=*/16u, /*add_num=*/1u);
    auto chunk = std::make_shared<BufferChunk>(pool);
    ASSERT_TRUE(chunk->Valid());
    SharedBufferSpan span(chunk, chunk->GetData() + chunk->GetLength(), chunk->GetData());
    EXPECT_FALSE(span.Valid());
    EXPECT_EQ(nullptr, span.GetStart());
    EXPECT_EQ(nullptr, span.GetEnd());
    EXPECT_EQ(nullptr, span.GetChunk());
}

// ============================================================================
// Extended BufferSpan tests
// ============================================================================

TEST(BufferSpanTest, DefaultConstructor) {
    BufferSpan span;
    EXPECT_FALSE(span.Valid());
    EXPECT_EQ(nullptr, span.GetStart());
    EXPECT_EQ(nullptr, span.GetEnd());
    EXPECT_EQ(0u, span.GetLength());
}

TEST(BufferSpanTest, CopyConstructor) {
    uint8_t storage[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    BufferSpan span1(storage, storage + 8);
    
    BufferSpan span2(span1);
    EXPECT_TRUE(span2.Valid());
    EXPECT_EQ(span1.GetStart(), span2.GetStart());
    EXPECT_EQ(span1.GetEnd(), span2.GetEnd());
    EXPECT_EQ(span1.GetLength(), span2.GetLength());
}

TEST(BufferSpanTest, Assignment) {
    uint8_t storage1[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t storage2[4] = {9, 10, 11, 12};
    
    BufferSpan span1(storage1, storage1 + 8);
    BufferSpan span2(storage2, storage2 + 4);
    
    span1 = span2;
    EXPECT_EQ(storage2, span1.GetStart());
    EXPECT_EQ(4u, span1.GetLength());
}

TEST(BufferSpanTest, ZeroLengthSpan) {
    uint8_t storage[4] = {1, 2, 3, 4};
    BufferSpan span(storage, storage);
    
    EXPECT_TRUE(span.Valid());
    EXPECT_EQ(0u, span.GetLength());
}

TEST(BufferSpanTest, LargeSpan) {
    std::vector<uint8_t> storage(4096);
    BufferSpan span(storage.data(), storage.data() + storage.size());
    
    EXPECT_TRUE(span.Valid());
    EXPECT_EQ(4096u, span.GetLength());
}

// ============================================================================
// Extended SharedBufferSpan tests
// ============================================================================

TEST(SharedBufferSpanTest, DefaultConstructor) {
    SharedBufferSpan span;
    EXPECT_FALSE(span.Valid());
    EXPECT_EQ(nullptr, span.GetStart());
    EXPECT_EQ(nullptr, span.GetEnd());
    EXPECT_EQ(0u, span.GetLength());
    EXPECT_EQ(nullptr, span.GetChunk());
}

TEST(SharedBufferSpanTest, ConstructorWithPointers) {
    auto pool = MakeBlockMemoryPoolPtr(32u, 1u);
    auto chunk = std::make_shared<BufferChunk>(pool);
    
    SharedBufferSpan span(chunk, chunk->GetData(), chunk->GetData() + 16);
    EXPECT_TRUE(span.Valid());
    EXPECT_EQ(16u, span.GetLength());
    EXPECT_EQ(chunk, span.GetChunk());
}

TEST(SharedBufferSpanTest, CopyConstructor) {
    auto pool = MakeBlockMemoryPoolPtr(32u, 1u);
    auto chunk = std::make_shared<BufferChunk>(pool);
    
    SharedBufferSpan span1(chunk, chunk->GetData(), 16u);
    SharedBufferSpan span2(span1);
    
    EXPECT_TRUE(span2.Valid());
    EXPECT_EQ(span1.GetStart(), span2.GetStart());
    EXPECT_EQ(span1.GetLength(), span2.GetLength());
    EXPECT_EQ(span1.GetChunk(), span2.GetChunk());
}

TEST(SharedBufferSpanTest, MoveConstructor) {
    auto pool = MakeBlockMemoryPoolPtr(32u, 1u);
    auto chunk = std::make_shared<BufferChunk>(pool);
    
    SharedBufferSpan span1(chunk, chunk->GetData(), 16u);
    auto* start = span1.GetStart();
    
    SharedBufferSpan span2(std::move(span1));
    EXPECT_TRUE(span2.Valid());
    EXPECT_EQ(start, span2.GetStart());
    EXPECT_EQ(16u, span2.GetLength());
}

TEST(SharedBufferSpanTest, Assignment) {
    auto pool = MakeBlockMemoryPoolPtr(32u, 2u);
    auto chunk1 = std::make_shared<BufferChunk>(pool);
    auto chunk2 = std::make_shared<BufferChunk>(pool);
    
    SharedBufferSpan span1(chunk1, chunk1->GetData(), 16u);
    SharedBufferSpan span2(chunk2, chunk2->GetData(), 8u);
    
    span1 = span2;
    EXPECT_EQ(chunk2, span1.GetChunk());
    EXPECT_EQ(8u, span1.GetLength());
}

TEST(SharedBufferSpanTest, MoveAssignment) {
    auto pool = MakeBlockMemoryPoolPtr(32u, 2u);
    auto chunk1 = std::make_shared<BufferChunk>(pool);
    auto chunk2 = std::make_shared<BufferChunk>(pool);
    
    SharedBufferSpan span1(chunk1, chunk1->GetData(), 16u);
    SharedBufferSpan span2(chunk2, chunk2->GetData(), 8u);
    auto* start2 = span2.GetStart();
    
    span1 = std::move(span2);
    EXPECT_EQ(start2, span1.GetStart());
    EXPECT_EQ(8u, span1.GetLength());
}

TEST(SharedBufferSpanTest, ZeroLengthSpan) {
    auto pool = MakeBlockMemoryPoolPtr(32u, 1u);
    auto chunk = std::make_shared<BufferChunk>(pool);
    
    SharedBufferSpan span(chunk, chunk->GetData(), 0u);
    EXPECT_TRUE(span.Valid());
    EXPECT_EQ(0u, span.GetLength());
}

TEST(SharedBufferSpanTest, NullChunk) {
    SharedBufferSpan span(nullptr, nullptr, 10u);
    EXPECT_FALSE(span.Valid());
}

TEST(SharedBufferSpanTest, ChunkLifetime) {
    auto pool = MakeBlockMemoryPoolPtr(32u, 1u);
    std::weak_ptr<BufferChunk> weak_chunk;
    
    SharedBufferSpan span;
    {
        auto chunk = std::make_shared<BufferChunk>(pool);
        weak_chunk = chunk;
        span = SharedBufferSpan(chunk, chunk->GetData(), 16u);
        EXPECT_FALSE(weak_chunk.expired());
    }
    
    // Span should keep chunk alive
    EXPECT_FALSE(weak_chunk.expired());
    EXPECT_TRUE(span.Valid());
    
    span = SharedBufferSpan();
    // Now chunk should be released
    EXPECT_TRUE(weak_chunk.expired());
}

}
}
}


