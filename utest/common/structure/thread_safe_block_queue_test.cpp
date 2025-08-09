#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <string>
#include <gtest/gtest.h>
#include "common/structure/thread_safe_block_queue.h"

namespace quicx {
namespace common {
namespace {

// Basic functionality tests
TEST(thread_safe_block_queue_utest, basic_operations) {
    ThreadSafeBlockQueue<int> queue;
    
    // Test initial state
    EXPECT_TRUE(queue.Empty());
    EXPECT_EQ(queue.Size(), 0);
    
    // Test Push and Pop
    queue.Push(1);
    EXPECT_FALSE(queue.Empty());
    EXPECT_EQ(queue.Size(), 1);
    
    int value = queue.Pop();
    EXPECT_EQ(value, 1);
    EXPECT_TRUE(queue.Empty());
    EXPECT_EQ(queue.Size(), 0);
}

TEST(thread_safe_block_queue_utest, multiple_elements) {
    ThreadSafeBlockQueue<int> queue;
    
    // Add multiple elements
    for (int i = 0; i < 10; ++i) {
        queue.Push(i);
        EXPECT_EQ(queue.Size(), i + 1);
    }
    
    // Pop elements in order
    for (int i = 0; i < 10; ++i) {
        int value = queue.Pop();
        EXPECT_EQ(value, i);
        EXPECT_EQ(queue.Size(), 9 - i);
    }
    
    EXPECT_TRUE(queue.Empty());
}

TEST(thread_safe_block_queue_utest, try_pop) {
    ThreadSafeBlockQueue<int> queue;
    
    // TryPop should return false when queue is empty
    int value;
    EXPECT_FALSE(queue.TryPop(value));
    
    // TryPop should succeed after adding an element
    queue.Push(42);
    EXPECT_TRUE(queue.TryPop(value));
    EXPECT_EQ(value, 42);
    
    // TryPop should fail again after popping
    EXPECT_FALSE(queue.TryPop(value));
}

TEST(thread_safe_block_queue_utest, try_pop_timeout) {
    ThreadSafeBlockQueue<int> queue;
    
    int value;
    auto start = std::chrono::steady_clock::now();
    
    // TryPop with timeout should timeout when queue is empty
    EXPECT_FALSE(queue.TryPop(value, std::chrono::milliseconds(100)));
    
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_GE(duration.count(), 100);
    
    // Should succeed immediately after adding an element
    queue.Push(123);
    start = std::chrono::steady_clock::now();
    EXPECT_TRUE(queue.TryPop(value, std::chrono::milliseconds(100)));
    end = std::chrono::steady_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_LT(duration.count(), 100);
    EXPECT_EQ(value, 123);
}

TEST(thread_safe_block_queue_utest, clear) {
    ThreadSafeBlockQueue<int> queue;
    
    // Add some elements
    for (int i = 0; i < 5; ++i) {
        queue.Push(i);
    }
    EXPECT_EQ(queue.Size(), 5);
    
    // Clear the queue
    queue.Clear();
    EXPECT_TRUE(queue.Empty());
    EXPECT_EQ(queue.Size(), 0);
    
    // TryPop should fail after clearing
    int value;
    EXPECT_FALSE(queue.TryPop(value));
}

// Thread safety tests
TEST(thread_safe_block_queue_utest, thread_safety_single_producer_single_consumer) {
    ThreadSafeBlockQueue<int> queue;
    std::atomic<bool> stop_flag(false);
    std::vector<int> received_values;
    std::mutex received_mutex;
    
    // Consumer thread (use timed TryPop to avoid indefinite blocking)
    std::thread consumer([&]() {
        while (!stop_flag.load() || !queue.Empty()) {
            int value;
            if (queue.TryPop(value, std::chrono::milliseconds(10))) {
                std::lock_guard<std::mutex> lock(received_mutex);
                received_values.push_back(value);
            }
        }
    });
    
    // Producer thread
    std::thread producer([&]() {
        for (int i = 0; i < 100; ++i) {
            queue.Push(i);
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
        stop_flag.store(true);
    });
    
    producer.join();
    consumer.join();
    
    // Verify all values were received correctly
    EXPECT_EQ(received_values.size(), 100);
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(received_values[i], i);
    }
}

TEST(thread_safe_block_queue_utest, thread_safety_multiple_producers) {
    ThreadSafeBlockQueue<int> queue;
    std::atomic<bool> stop_flag(false);
    std::atomic<int> total_received(0);
    std::vector<std::thread> producers;
    
    // Start multiple producer threads
    for (int i = 0; i < 4; ++i) {
        producers.emplace_back([&, i]() {
            for (int j = 0; j < 25; ++j) {
                queue.Push(i * 25 + j);
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        });
    }
    
    // Consumer thread
    std::thread consumer([&]() {
        while (total_received.load() < 100) {
            int value = queue.Pop();
            total_received.fetch_add(1);
        }
    });
    
    // Wait for all threads to complete
    for (auto& producer : producers) {
        producer.join();
    }
    consumer.join();
    
    EXPECT_EQ(total_received.load(), 100);
}

TEST(thread_safe_block_queue_utest, thread_safety_multiple_consumers) {
    ThreadSafeBlockQueue<int> queue;
    std::atomic<bool> stop_flag(false);
    std::atomic<int> total_received(0);
    std::vector<std::thread> consumers;
    
    // Start multiple consumer threads (use timed TryPop to avoid indefinite blocking)
    for (int i = 0; i < 4; ++i) {
        consumers.emplace_back([&]() {
            while (!stop_flag.load() || !queue.Empty()) {
                int value;
                if (queue.TryPop(value, std::chrono::milliseconds(10))) {
                    total_received.fetch_add(1);
                }
            }
        });
    }
    
    // Producer thread
    std::thread producer([&]() {
        for (int i = 0; i < 100; ++i) {
            queue.Push(i);
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
        stop_flag.store(true);
    });
    
    producer.join();
    for (auto& consumer : consumers) {
        consumer.join();
    }
    
    EXPECT_EQ(total_received.load(), 100);
}

// Boundary condition tests
TEST(thread_safe_block_queue_utest, large_number_of_elements) {
    ThreadSafeBlockQueue<int> queue;
    const int large_count = 10000;
    
    // Add large number of elements
    for (int i = 0; i < large_count; ++i) {
        queue.Push(i);
    }
    EXPECT_EQ(queue.Size(), large_count);
    
    // Pop all elements
    for (int i = 0; i < large_count; ++i) {
        int value = queue.Pop();
        EXPECT_EQ(value, i);
    }
    EXPECT_TRUE(queue.Empty());
}

TEST(thread_safe_block_queue_utest, stress_test) {
    ThreadSafeBlockQueue<int> queue;
    std::atomic<bool> stop_flag(false);
    std::atomic<int> total_produced(0);
    std::atomic<int> total_consumed(0);
    
    const int num_producers = 4;
    const int num_consumers = 4;
    const int elements_per_producer = 1000;
    
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    
    // Start producers
    for (int i = 0; i < num_producers; ++i) {
        producers.emplace_back([&, i]() {
            for (int j = 0; j < elements_per_producer; ++j) {
                queue.Push(i * elements_per_producer + j);
                total_produced.fetch_add(1);
            }
        });
    }
    
    // Start consumers (use timed TryPop to avoid indefinite blocking)
    for (int i = 0; i < num_consumers; ++i) {
        consumers.emplace_back([&]() {
            const int target = num_producers * elements_per_producer;
            while (total_consumed.load() < target || !queue.Empty()) {
                int value;
                if (queue.TryPop(value, std::chrono::milliseconds(10))) {
                    total_consumed.fetch_add(1);
                }
            }
        });
    }
    
    // Wait for all threads to complete
    for (auto& producer : producers) {
        producer.join();
    }
    for (auto& consumer : consumers) {
        consumer.join();
    }
    
    EXPECT_EQ(total_produced.load(), num_producers * elements_per_producer);
    EXPECT_EQ(total_consumed.load(), num_producers * elements_per_producer);
    EXPECT_TRUE(queue.Empty());
}

// Custom type tests
struct TestData {
    int id;
    std::string name;
    
    TestData() : id(0) {}
    TestData(int i, const std::string& n) : id(i), name(n) {}
    
    bool operator==(const TestData& other) const {
        return id == other.id && name == other.name;
    }
};

TEST(thread_safe_block_queue_utest, custom_type) {
    ThreadSafeBlockQueue<TestData> queue;
    
    TestData data1(1, "test1");
    TestData data2(2, "test2");
    
    queue.Push(data1);
    queue.Push(data2);
    
    EXPECT_EQ(queue.Size(), 2);
    
    TestData received1 = queue.Pop();
    TestData received2 = queue.Pop();
    
    EXPECT_EQ(received1, data1);
    EXPECT_EQ(received2, data2);
    EXPECT_TRUE(queue.Empty());
}

}
}
}
