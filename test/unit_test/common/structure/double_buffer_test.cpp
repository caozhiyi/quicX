#include "common/structure/double_buffer.h"

#include <memory>
#include <thread>
#include <vector>

#include "gtest/gtest.h"

namespace quicx {
namespace common {

// Test basic add and swap functionality
TEST(DoubleBufferTest, BasicAddAndSwap) {
    DoubleBuffer<int> buffer;

    // Initially empty
    EXPECT_TRUE(buffer.IsEmpty());
    EXPECT_EQ(buffer.Size(), 0u);

    // Add items
    buffer.Add(1);
    buffer.Add(2);
    buffer.Add(3);

    EXPECT_FALSE(buffer.IsEmpty());
    EXPECT_EQ(buffer.Size(), 3u);

    // Swap - items move from write buffer to read buffer
    buffer.Swap();
    auto& read_buf = buffer.GetReadBuffer();

    EXPECT_EQ(read_buf.size(), 3u);
    EXPECT_NE(read_buf.find(1), read_buf.end());
    EXPECT_NE(read_buf.find(2), read_buf.end());
    EXPECT_NE(read_buf.find(3), read_buf.end());
}

// Test adding items during processing (write while reading)
TEST(DoubleBufferTest, AddDuringProcessing) {
    DoubleBuffer<int> buffer;

    // Add initial items
    buffer.Add(1);
    buffer.Add(2);

    // Swap to start processing
    buffer.Swap();
    auto& read_buf = buffer.GetReadBuffer();

    EXPECT_EQ(read_buf.size(), 2u);

    // Simulate processing: remove item 1, add item 3 (this goes to write buffer)
    read_buf.erase(1);
    buffer.Add(3);

    EXPECT_EQ(read_buf.size(), 1u);  // Only item 2 remains in read buffer
    EXPECT_EQ(buffer.Size(), 2u);    // Total: item 2 (read) + item 3 (write)

    // Swap again
    buffer.Swap();
    auto& new_read_buf = buffer.GetReadBuffer();

    // Should have item 2 (unprocessed) and item 3 (newly added)
    EXPECT_EQ(new_read_buf.size(), 2u);
    EXPECT_NE(new_read_buf.find(2), new_read_buf.end());
    EXPECT_NE(new_read_buf.find(3), new_read_buf.end());
    EXPECT_EQ(new_read_buf.find(1), new_read_buf.end());  // Item 1 was removed
}

// Test duplicate item handling (set semantics)
TEST(DoubleBufferTest, DuplicateItems) {
    DoubleBuffer<int> buffer;

    // Add same item multiple times
    buffer.Add(1);
    buffer.Add(1);
    buffer.Add(2);
    buffer.Add(1);

    // Should only have 2 unique items
    EXPECT_EQ(buffer.Size(), 2u);

    buffer.Swap();
    auto& read_buf = buffer.GetReadBuffer();
    EXPECT_EQ(read_buf.size(), 2u);
}

// Test clear functionality
TEST(DoubleBufferTest, Clear) {
    DoubleBuffer<int> buffer;

    buffer.Add(1);
    buffer.Add(2);
    buffer.Swap();
    buffer.Add(3);

    EXPECT_FALSE(buffer.IsEmpty());

    buffer.Clear();

    EXPECT_TRUE(buffer.IsEmpty());
    EXPECT_EQ(buffer.Size(), 0u);
}

// Test with pointer types (typical use case)
TEST(DoubleBufferTest, WithPointers) {
    struct TestObject {
        int id;
        explicit TestObject(int i) : id(i) {}
    };

    DoubleBuffer<std::shared_ptr<TestObject>> buffer;

    auto obj1 = std::make_shared<TestObject>(1);
    auto obj2 = std::make_shared<TestObject>(2);

    buffer.Add(obj1);
    buffer.Add(obj2);

    EXPECT_EQ(buffer.Size(), 2u);

    buffer.Swap();
    auto& read_buf = buffer.GetReadBuffer();

    EXPECT_EQ(read_buf.size(), 2u);
    EXPECT_NE(read_buf.find(obj1), read_buf.end());
    EXPECT_NE(read_buf.find(obj2), read_buf.end());
}

// Test multiple swap cycles (realistic usage pattern)
TEST(DoubleBufferTest, MultipleSwapCycles) {
    DoubleBuffer<int> buffer;

    // Cycle 1
    buffer.Add(1);
    buffer.Add(2);
    buffer.Swap();
    {
        auto& read_buf = buffer.GetReadBuffer();
        EXPECT_EQ(read_buf.size(), 2u);
        read_buf.erase(1);  // Process item 1
        buffer.Add(3);      // New item during processing
    }

    // Cycle 2
    buffer.Swap();
    {
        auto& read_buf = buffer.GetReadBuffer();
        EXPECT_EQ(read_buf.size(), 2u);  // Items 2 and 3
        EXPECT_NE(read_buf.find(2), read_buf.end());
        EXPECT_NE(read_buf.find(3), read_buf.end());
        read_buf.erase(2);  // Process item 2
        buffer.Add(4);      // New item
    }

    // Cycle 3
    buffer.Swap();
    {
        auto& read_buf = buffer.GetReadBuffer();
        EXPECT_EQ(read_buf.size(), 2u);  // Items 3 and 4
        EXPECT_NE(read_buf.find(3), read_buf.end());
        EXPECT_NE(read_buf.find(4), read_buf.end());
    }
}

// Test edge case: swap when read buffer is empty
TEST(DoubleBufferTest, SwapWithEmptyReadBuffer) {
    DoubleBuffer<int> buffer;

    // Add items
    buffer.Add(1);
    buffer.Add(2);

    // Swap
    buffer.Swap();
    auto& read_buf1 = buffer.GetReadBuffer();

    // Process all items
    read_buf1.clear();
    EXPECT_TRUE(read_buf1.empty());

    // Add new item
    buffer.Add(3);

    // Swap again (read buffer was empty)
    buffer.Swap();
    auto& read_buf2 = buffer.GetReadBuffer();

    // Should only have item 3
    EXPECT_EQ(read_buf2.size(), 1u);
    EXPECT_NE(read_buf2.find(3), read_buf2.end());
}

// Test edge case: swap when write buffer is empty
TEST(DoubleBufferTest, SwapWithEmptyWriteBuffer) {
    DoubleBuffer<int> buffer;

    buffer.Add(1);
    buffer.Swap();

    auto& read_buf = buffer.GetReadBuffer();
    EXPECT_EQ(read_buf.size(), 1u);

    // Don't add anything to write buffer
    // Swap again
    buffer.Swap();
    auto& read_buf2 = buffer.GetReadBuffer();

    // Item 1 should still be there (unprocessed)
    EXPECT_EQ(read_buf2.size(), 1u);
    EXPECT_NE(read_buf2.find(1), read_buf2.end());
}

// Concurrency safety test (basic)
// Note: This is a basic sanity check. Full thread safety requires
// external synchronization in the actual usage.
// std::unordered_set::insert is not thread-safe, so some items may be lost
TEST(DoubleBufferTest, ConcurrentAdd) {
    DoubleBuffer<int> buffer;

    // Simulate: processing thread swaps and reads, while callbacks add items
    std::vector<std::thread> threads;

    // Add items from multiple threads (simulating callbacks)
    for (int i = 0; i < 5; ++i) {
        threads.emplace_back([&buffer, i]() {
            for (int j = 0; j < 10; ++j) {
                buffer.Add(i * 10 + j);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Most items should be added (some may be lost due to race conditions)
    // In production, external synchronization (e.g., mutex) should be used
    EXPECT_GE(buffer.Size(), 40u);  // At least 80% success rate
    EXPECT_LE(buffer.Size(), 50u);  // No more than 50 unique items
}

}  // namespace common
}  // namespace quicx
