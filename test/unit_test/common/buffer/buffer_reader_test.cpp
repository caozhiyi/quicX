#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_chunk.h"
#include "common/buffer/buffer_reader.h"
#include "common/buffer/multi_block_buffer.h"
#include "common/buffer/single_block_buffer.h"

namespace quicx {
namespace common {
namespace {

// Helper function to create a pool
std::shared_ptr<BlockMemoryPool> MakePool(uint32_t size = 64, uint32_t count = 4) {
    return MakeBlockMemoryPoolPtr(size, count);
}

// ============================================================================
// Contiguous Mode - Basic Construction and Validity
// ============================================================================

TEST(BufferReaderContiguousTest, DefaultConstruction) {
    BufferReader reader;
    EXPECT_FALSE(reader.Valid());
    EXPECT_EQ(0u, reader.GetDataLength());
    EXPECT_TRUE(reader.IsContiguous());
}

TEST(BufferReaderContiguousTest, PointerConstruction) {
    uint8_t storage[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    BufferReader reader(storage, storage + 8);
    EXPECT_TRUE(reader.Valid());
    EXPECT_TRUE(reader.IsContiguous());
    EXPECT_EQ(8u, reader.GetDataLength());
    EXPECT_EQ(storage, reader.GetData());
}

TEST(BufferReaderContiguousTest, LengthConstruction) {
    uint8_t storage[6] = {10, 20, 30, 40, 50, 60};
    BufferReader reader(storage, 6u);
    EXPECT_TRUE(reader.Valid());
    EXPECT_EQ(6u, reader.GetDataLength());
    EXPECT_EQ(storage, reader.GetData());
}

TEST(BufferReaderContiguousTest, ZeroLengthSpan) {
    uint8_t storage[4] = {1, 2, 3, 4};
    BufferReader reader(storage, storage);
    EXPECT_TRUE(reader.Valid());
    EXPECT_EQ(0u, reader.GetDataLength());
}

// ============================================================================
// Contiguous Mode - Read Operations
// ============================================================================

TEST(BufferReaderContiguousTest, BasicReadAndMove) {
    uint8_t storage[6] = {10, 11, 12, 13, 14, 15};
    BufferReader reader(storage, 6u);
    EXPECT_EQ(6u, reader.GetDataLength());

    uint8_t tmp[3] = {};
    EXPECT_EQ(3u, reader.ReadNotMovePt(tmp, 3));
    EXPECT_TRUE(std::equal(tmp, tmp + 3, storage));
    EXPECT_EQ(6u, reader.GetDataLength());

    EXPECT_EQ(2u, reader.MoveReadPt(2));
    EXPECT_EQ(4u, reader.GetDataLength());
    EXPECT_EQ(storage + 2, reader.GetData());

    uint8_t out[10] = {};
    EXPECT_EQ(4u, reader.Read(out, sizeof(out)));
    EXPECT_TRUE(std::equal(out, out + 4, storage + 2));
    EXPECT_EQ(0u, reader.GetDataLength());
}

TEST(BufferReaderContiguousTest, ReadWithNullptr) {
    uint8_t storage[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    BufferReader reader(storage, 8u);
    EXPECT_EQ(0u, reader.Read(nullptr, 5));
    EXPECT_EQ(8u, reader.GetDataLength());
}

TEST(BufferReaderContiguousTest, ReadNotMovePtWithNullptr) {
    uint8_t storage[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    BufferReader reader(storage, 8u);
    EXPECT_EQ(0u, reader.ReadNotMovePt(nullptr, 5));
    EXPECT_EQ(8u, reader.GetDataLength());
}

TEST(BufferReaderContiguousTest, ReadExceedingAvailable) {
    uint8_t storage[6] = {10, 20, 30, 40, 50, 60};
    BufferReader reader(storage, 6u);

    uint8_t buffer[20] = {};
    EXPECT_EQ(6u, reader.Read(buffer, 20));
    EXPECT_EQ(0u, reader.GetDataLength());
    EXPECT_TRUE(std::equal(storage, storage + 6, buffer));
}

TEST(BufferReaderContiguousTest, MoveReadPtBeyondEnd) {
    uint8_t storage[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    BufferReader reader(storage, 8u);
    EXPECT_EQ(8u, reader.MoveReadPt(100));
    EXPECT_EQ(0u, reader.GetDataLength());
}

TEST(BufferReaderContiguousTest, SequentialReads) {
    uint8_t storage[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    BufferReader reader(storage, 12u);

    uint8_t buf1[4];
    EXPECT_EQ(4u, reader.Read(buf1, 4));
    EXPECT_TRUE(std::equal(storage, storage + 4, buf1));

    uint8_t buf2[5];
    EXPECT_EQ(5u, reader.Read(buf2, 5));
    EXPECT_TRUE(std::equal(storage + 4, storage + 9, buf2));

    uint8_t buf3[10];
    EXPECT_EQ(3u, reader.Read(buf3, 10));
    EXPECT_TRUE(std::equal(storage + 9, storage + 12, buf3));
}

TEST(BufferReaderContiguousTest, VisitDataCallback) {
    uint8_t storage[6] = {11, 22, 33, 44, 55, 66};
    BufferReader reader(storage, 6u);

    size_t visit_count = 0;
    reader.VisitData([&](uint8_t* data, uint32_t len) {
        EXPECT_EQ(6u, len);
        EXPECT_EQ(0, std::memcmp(data, storage, len));
        visit_count++;
        return true;
    });
    EXPECT_EQ(1u, visit_count);
}

TEST(BufferReaderContiguousTest, VisitDataWithNullCallback) {
    uint8_t storage[6] = {1, 2, 3, 4, 5, 6};
    BufferReader reader(storage, 6u);
    reader.VisitData(nullptr);
}

TEST(BufferReaderContiguousTest, VisitDataOnEmptyReader) {
    uint8_t storage[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    BufferReader reader(storage, 8u);

    uint8_t buffer[8];
    reader.Read(buffer, 8);

    size_t visit_count = 0;
    reader.VisitData([&](uint8_t*, uint32_t) { visit_count++; return true; });
    EXPECT_EQ(0u, visit_count);
}

TEST(BufferReaderContiguousTest, GetReadableSpan) {
    uint8_t storage[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    BufferReader reader(storage, 8u);

    auto span = reader.GetReadableSpan();
    EXPECT_TRUE(span.Valid());
    EXPECT_EQ(8u, span.GetLength());
    EXPECT_EQ(storage, span.GetStart());

    uint8_t buffer[3];
    reader.Read(buffer, 3);

    auto span2 = reader.GetReadableSpan();
    EXPECT_EQ(5u, span2.GetLength());
    EXPECT_EQ(storage + 3, span2.GetStart());
}

TEST(BufferReaderContiguousTest, GetReadableSpanOnInvalid) {
    BufferReader reader;
    auto span = reader.GetReadableSpan();
    EXPECT_FALSE(span.Valid());
}

TEST(BufferReaderContiguousTest, ClearReader) {
    uint8_t storage[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    BufferReader reader(storage, 8u);
    EXPECT_TRUE(reader.Valid());

    reader.Clear();
    EXPECT_FALSE(reader.Valid());
    EXPECT_EQ(nullptr, reader.GetData());
    EXPECT_EQ(0u, reader.GetDataLength());
}

TEST(BufferReaderContiguousTest, GetReadOffset) {
    uint8_t storage[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    BufferReader reader(storage, 8u);
    EXPECT_EQ(0u, reader.GetReadOffset());

    reader.MoveReadPt(3);
    EXPECT_EQ(3u, reader.GetReadOffset());

    reader.MoveReadPt(5);
    EXPECT_EQ(8u, reader.GetReadOffset());
}

TEST(BufferReaderContiguousTest, ResetToNewRange) {
    uint8_t storage1[4] = {1, 2, 3, 4};
    uint8_t storage2[8] = {5, 6, 7, 8, 9, 10, 11, 12};
    BufferReader reader(storage1, 4u);
    EXPECT_EQ(4u, reader.GetDataLength());

    reader.Reset(storage2, storage2 + 8);
    EXPECT_TRUE(reader.IsContiguous());
    EXPECT_EQ(8u, reader.GetDataLength());
    EXPECT_EQ(storage2, reader.GetData());
}

// ============================================================================
// IBuffer Mode - Basic Construction and Validity
// ============================================================================

TEST(BufferReaderIBufferTest, DefaultConstruction) {
    BufferReader reader;
    EXPECT_FALSE(reader.Valid());
    EXPECT_EQ(0u, reader.GetDataLength());
}

TEST(BufferReaderIBufferTest, ConstructionWithBuffer) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));

    BufferReader reader(buffer);
    EXPECT_TRUE(reader.Valid());
    EXPECT_FALSE(reader.IsContiguous());
    EXPECT_EQ(5u, reader.GetDataLength());
    EXPECT_EQ(0u, reader.GetReadOffset());
}

TEST(BufferReaderIBufferTest, Reset) {
    auto pool = MakePool(64, 4);
    auto buffer1 = std::make_shared<MultiBlockBuffer>(pool);
    auto buffer2 = std::make_shared<MultiBlockBuffer>(pool);

    buffer1->Write(reinterpret_cast<const uint8_t*>("hello"), 5);
    buffer2->Write(reinterpret_cast<const uint8_t*>("world"), 5);

    BufferReader reader(buffer1);
    EXPECT_EQ(5u, reader.GetDataLength());

    reader.Reset(buffer2);
    EXPECT_EQ(5u, reader.GetDataLength());
    EXPECT_EQ(0u, reader.GetReadOffset());
}

TEST(BufferReaderIBufferTest, Clear) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);
    buffer->Write(reinterpret_cast<const uint8_t*>("test"), 4);

    BufferReader reader(buffer);
    EXPECT_TRUE(reader.Valid());

    reader.Clear();
    EXPECT_FALSE(reader.Valid());
    EXPECT_EQ(0u, reader.GetDataLength());
    EXPECT_EQ(0u, reader.GetReadOffset());
}

// ============================================================================
// IBuffer Mode - ReadNotMovePt Tests
// ============================================================================

TEST(BufferReaderIBufferTest, ReadNotMovePtBasic) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    std::vector<uint8_t> data = {1, 2, 3, 4, 5, 6, 7, 8};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));

    BufferReader reader(buffer);

    uint8_t out[8] = {0};
    uint32_t read = reader.ReadNotMovePt(out, 8);
    EXPECT_EQ(8u, read);
    EXPECT_EQ(0, memcmp(out, data.data(), 8));

    // Read again should get same data (pointer not moved)
    uint8_t out2[8] = {0};
    read = reader.ReadNotMovePt(out2, 8);
    EXPECT_EQ(8u, read);
    EXPECT_EQ(0, memcmp(out2, data.data(), 8));
    EXPECT_EQ(0u, reader.GetReadOffset());
}

TEST(BufferReaderIBufferTest, ReadNotMovePtPartial) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));

    BufferReader reader(buffer);

    uint8_t out[3] = {0};
    uint32_t read = reader.ReadNotMovePt(out, 3);
    EXPECT_EQ(3u, read);
    EXPECT_EQ(1u, out[0]);
    EXPECT_EQ(2u, out[1]);
    EXPECT_EQ(3u, out[2]);
    EXPECT_EQ(0u, reader.GetReadOffset());
}

TEST(BufferReaderIBufferTest, ReadNotMovePtAfterMove) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    std::vector<uint8_t> data = {1, 2, 3, 4, 5, 6, 7, 8};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));

    BufferReader reader(buffer);

    reader.MoveReadPt(3);
    EXPECT_EQ(3u, reader.GetReadOffset());

    uint8_t out[5] = {0};
    uint32_t read = reader.ReadNotMovePt(out, 5);
    EXPECT_EQ(5u, read);
    EXPECT_EQ(4u, out[0]);
    EXPECT_EQ(5u, out[1]);
    EXPECT_EQ(6u, out[2]);
    EXPECT_EQ(7u, out[3]);
    EXPECT_EQ(8u, out[4]);
}

TEST(BufferReaderIBufferTest, ReadNotMovePtEmptyBuffer) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    BufferReader reader(buffer);

    uint8_t out[10] = {0};
    uint32_t read = reader.ReadNotMovePt(out, 10);
    EXPECT_EQ(0u, read);
}

TEST(BufferReaderIBufferTest, ReadNotMovePtInvalidReader) {
    BufferReader reader;

    uint8_t out[10] = {0};
    uint32_t read = reader.ReadNotMovePt(out, 10);
    EXPECT_EQ(0u, read);
}

// ============================================================================
// IBuffer Mode - Read Tests (moves pointer)
// ============================================================================

TEST(BufferReaderIBufferTest, ReadBasic) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));

    BufferReader reader(buffer);

    uint8_t out[5] = {0};
    uint32_t read = reader.Read(out, 5);
    EXPECT_EQ(5u, read);
    EXPECT_EQ(0, memcmp(out, data.data(), 5));
    EXPECT_EQ(5u, reader.GetReadOffset());
    EXPECT_EQ(0u, reader.GetDataLength());
}

TEST(BufferReaderIBufferTest, ReadPartial) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    std::vector<uint8_t> data = {1, 2, 3, 4, 5, 6, 7, 8};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));

    BufferReader reader(buffer);

    uint8_t out[3] = {0};
    uint32_t read = reader.Read(out, 3);
    EXPECT_EQ(3u, read);
    EXPECT_EQ(3u, reader.GetReadOffset());
    EXPECT_EQ(5u, reader.GetDataLength());

    uint8_t out2[5] = {0};
    read = reader.Read(out2, 5);
    EXPECT_EQ(5u, read);
    EXPECT_EQ(8u, reader.GetReadOffset());
    EXPECT_EQ(0u, reader.GetDataLength());
}

TEST(BufferReaderIBufferTest, ReadMultipleTimes) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    std::vector<uint8_t> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));

    BufferReader reader(buffer);

    for (uint32_t i = 0; i < 10; ++i) {
        uint8_t out = 0;
        uint32_t read = reader.Read(&out, 1);
        EXPECT_EQ(1u, read);
        EXPECT_EQ(static_cast<uint8_t>(i + 1), out);
        EXPECT_EQ(i + 1, reader.GetReadOffset());
    }

    EXPECT_EQ(10u, reader.GetReadOffset());
    EXPECT_EQ(0u, reader.GetDataLength());
}

// ============================================================================
// IBuffer Mode - MoveReadPt Tests
// ============================================================================

TEST(BufferReaderIBufferTest, MoveReadPtBasic) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));

    BufferReader reader(buffer);

    uint32_t moved = reader.MoveReadPt(3);
    EXPECT_EQ(3u, moved);
    EXPECT_EQ(3u, reader.GetReadOffset());
    EXPECT_EQ(2u, reader.GetDataLength());
}

TEST(BufferReaderIBufferTest, MoveReadPtBeyondEnd) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    std::vector<uint8_t> data = {1, 2, 3};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));

    BufferReader reader(buffer);

    uint32_t moved = reader.MoveReadPt(10);
    EXPECT_EQ(3u, moved);
    EXPECT_EQ(3u, reader.GetReadOffset());
    EXPECT_EQ(0u, reader.GetDataLength());
}

TEST(BufferReaderIBufferTest, MoveReadPtZero) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    std::vector<uint8_t> data = {1, 2, 3};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));

    BufferReader reader(buffer);

    uint32_t moved = reader.MoveReadPt(0);
    EXPECT_EQ(0u, moved);
    EXPECT_EQ(0u, reader.GetReadOffset());
}

// ============================================================================
// IBuffer Mode - Sync Tests
// ============================================================================

TEST(BufferReaderIBufferTest, SyncBasic) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));

    EXPECT_EQ(5u, buffer->GetDataLength());

    BufferReader reader(buffer);
    reader.MoveReadPt(3);
    EXPECT_EQ(3u, reader.GetReadOffset());
    EXPECT_EQ(5u, buffer->GetDataLength());

    reader.Sync();
    EXPECT_EQ(0u, reader.GetReadOffset());
    EXPECT_EQ(2u, buffer->GetDataLength());
}

TEST(BufferReaderIBufferTest, SyncNoOffset) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    std::vector<uint8_t> data = {1, 2, 3};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));

    BufferReader reader(buffer);
    EXPECT_EQ(0u, reader.GetReadOffset());

    reader.Sync();
    EXPECT_EQ(0u, reader.GetReadOffset());
    EXPECT_EQ(3u, buffer->GetDataLength());
}

// ============================================================================
// IBuffer Mode - VisitData Tests
// ============================================================================

TEST(BufferReaderIBufferTest, VisitDataBasic) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));

    BufferReader reader(buffer);

    std::vector<uint8_t> visited;
    reader.VisitData([&](uint8_t* d, uint32_t len) {
        visited.insert(visited.end(), d, d + len);
        return true;
    });

    EXPECT_EQ(5u, visited.size());
    EXPECT_EQ(0, memcmp(visited.data(), data.data(), 5));
}

TEST(BufferReaderIBufferTest, VisitDataAfterMove) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    std::vector<uint8_t> data = {1, 2, 3, 4, 5, 6, 7, 8};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));

    BufferReader reader(buffer);
    reader.MoveReadPt(3);

    std::vector<uint8_t> visited;
    reader.VisitData([&](uint8_t* d, uint32_t len) {
        visited.insert(visited.end(), d, d + len);
        return true;
    });

    EXPECT_EQ(5u, visited.size());
    EXPECT_EQ(4u, visited[0]);
    EXPECT_EQ(5u, visited[1]);
    EXPECT_EQ(6u, visited[2]);
    EXPECT_EQ(7u, visited[3]);
    EXPECT_EQ(8u, visited[4]);
}

TEST(BufferReaderIBufferTest, VisitDataEmpty) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    BufferReader reader(buffer);

    bool called = false;
    reader.VisitData([&](uint8_t*, uint32_t) {
        called = true;
        return true;
    });

    EXPECT_FALSE(called);
}

// ============================================================================
// IBuffer Mode - Multi-Block Tests
// ============================================================================

TEST(BufferReaderIBufferTest, ReadAcrossMultipleBlocks) {
    auto pool = MakePool(32, 4);  // Small chunk size to force multiple blocks
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    std::vector<uint8_t> data1(20, 0xAA);
    std::vector<uint8_t> data2(20, 0xBB);

    buffer->Write(data1.data(), 20);
    buffer->Write(data2.data(), 20);

    EXPECT_EQ(40u, buffer->GetDataLength());

    BufferReader reader(buffer);

    uint8_t out[30] = {0};
    uint32_t read = reader.ReadNotMovePt(out, 30);
    EXPECT_EQ(30u, read);

    for (uint32_t i = 0; i < 20; ++i) {
        EXPECT_EQ(0xAA, out[i]);
    }
    for (uint32_t i = 20; i < 30; ++i) {
        EXPECT_EQ(0xBB, out[i]);
    }
}

TEST(BufferReaderIBufferTest, ReadLargeDataAcrossBlocks) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);

    std::vector<uint8_t> large_data(200);
    for (size_t i = 0; i < large_data.size(); ++i) {
        large_data[i] = static_cast<uint8_t>(i % 256);
    }
    buffer->Write(large_data.data(), static_cast<uint32_t>(large_data.size()));

    BufferReader reader(buffer);

    std::vector<uint8_t> out(200);
    uint32_t read = reader.Read(out.data(), 200);
    EXPECT_EQ(200u, read);
    EXPECT_EQ(0, memcmp(out.data(), large_data.data(), 200));
    EXPECT_EQ(200u, reader.GetReadOffset());
}

// ============================================================================
// IBuffer Mode - Integration with SingleBlockBuffer
// ============================================================================

TEST(BufferReaderIBufferTest, WithSingleBlockBuffer) {
    auto pool = MakePool(64, 4);
    auto chunk = std::make_shared<BufferChunk>(pool);
    auto buffer = std::make_shared<SingleBlockBuffer>(chunk);

    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));

    BufferReader reader(buffer);

    uint8_t out[5] = {0};
    uint32_t read = reader.Read(out, 5);
    EXPECT_EQ(5u, read);
    EXPECT_EQ(0, memcmp(out, data.data(), 5));
}

// ============================================================================
// Mode Switching via Reset
// ============================================================================

TEST(BufferReaderModeTest, SwitchFromContiguousToIBuffer) {
    uint8_t storage[4] = {1, 2, 3, 4};
    BufferReader reader(storage, 4u);
    EXPECT_TRUE(reader.IsContiguous());
    EXPECT_EQ(4u, reader.GetDataLength());

    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);
    std::vector<uint8_t> data = {5, 6, 7, 8, 9};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));

    reader.Reset(buffer);
    EXPECT_FALSE(reader.IsContiguous());
    EXPECT_EQ(5u, reader.GetDataLength());

    uint8_t out[5] = {0};
    reader.Read(out, 5);
    EXPECT_EQ(5u, out[0]);
}

TEST(BufferReaderModeTest, SwitchFromIBufferToContiguous) {
    auto pool = MakePool(64, 4);
    auto buffer = std::make_shared<MultiBlockBuffer>(pool);
    std::vector<uint8_t> data = {1, 2, 3};
    buffer->Write(data.data(), static_cast<uint32_t>(data.size()));

    BufferReader reader(buffer);
    EXPECT_FALSE(reader.IsContiguous());

    uint8_t storage[8] = {10, 20, 30, 40, 50, 60, 70, 80};
    reader.Reset(storage, 8u);
    EXPECT_TRUE(reader.IsContiguous());
    EXPECT_EQ(8u, reader.GetDataLength());

    uint8_t out[1] = {0};
    reader.Read(out, 1);
    EXPECT_EQ(10u, out[0]);
}

}  // namespace
}  // namespace common
}  // namespace quicx
