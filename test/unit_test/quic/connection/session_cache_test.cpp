#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "common/util/time.h"
#include "quic/connection/session_cache.h"

namespace quicx {
namespace quic {

class SessionCacheTest: public ::testing::Test {
protected:
    void SetUp() override {
        // Reset SessionCache to clean state
        SessionCache::Instance().Reset();

        // Create temporary test directory
        test_cache_dir_ = std::filesystem::temp_directory_path() / "session_cache_test";
        if (std::filesystem::exists(test_cache_dir_)) {
            std::filesystem::remove_all(test_cache_dir_);
        }
        std::filesystem::create_directories(test_cache_dir_);
    }

    void TearDown() override {
        // Clean up test directory
        if (std::filesystem::exists(test_cache_dir_)) {
            std::filesystem::remove_all(test_cache_dir_);
        }
    }

    // Helper method to create a test session
    SessionInfo CreateTestSession(
        const std::string& server_name, uint64_t creation_time, uint32_t timeout, bool early_data_capable) {
        SessionInfo info;
        info.server_name = server_name;
        info.creation_time = creation_time;
        info.timeout = timeout;
        info.early_data_capable = early_data_capable;
        return info;
    }

    // Helper method to create test session DER data
    std::string CreateTestSessionDER(const std::string& server_name) {
        return "test_session_der_data_for_" + server_name;
    }

    // Helper method to wait for a short time
    void WaitForTime(uint32_t seconds) { std::this_thread::sleep_for(std::chrono::seconds(seconds)); }

    std::filesystem::path test_cache_dir_;
};

// Test basic initialization
TEST_F(SessionCacheTest, BasicInitialization) {
    SessionCache& cache = SessionCache::Instance();

    EXPECT_TRUE(cache.Init(test_cache_dir_.string()));
    EXPECT_EQ(cache.GetCacheSize(), 0);
    EXPECT_EQ(cache.GetMaxCacheSize(), 100);  // Default value
}

// Test storing and retrieving sessions
TEST_F(SessionCacheTest, StoreAndRetrieveSession) {
    SessionCache& cache = SessionCache::Instance();
    EXPECT_TRUE(cache.Init(test_cache_dir_.string()));

    std::string server_name = "example.com";
    std::string session_der = CreateTestSessionDER(server_name);
    SessionInfo info = CreateTestSession(server_name, common::UTCTimeMsec() / 1000, 3600, true);

    // Store session
    EXPECT_TRUE(cache.StoreSession(session_der, info));
    EXPECT_EQ(cache.GetCacheSize(), 1);

    // Retrieve session
    std::string retrieved_der;
    EXPECT_TRUE(cache.GetSession(server_name, retrieved_der));
    EXPECT_EQ(retrieved_der, session_der);
}

// Test 0-RTT session validation
TEST_F(SessionCacheTest, ZeroRTTValidation) {
    SessionCache& cache = SessionCache::Instance();
    EXPECT_TRUE(cache.Init(test_cache_dir_.string()));

    std::string server_name = "example.com";
    std::string session_der = CreateTestSessionDER(server_name);

    // Create session with 0-RTT capability
    SessionInfo info = CreateTestSession(server_name, common::UTCTimeMsec() / 1000, 3600, true);
    EXPECT_TRUE(cache.StoreSession(session_der, info));

    // Should be valid for 0-RTT
    EXPECT_TRUE(cache.HasValidSessionFor0RTT(server_name));

    // Create session without 0-RTT capability
    std::string server_name2 = "example2.com";
    std::string session_der2 = CreateTestSessionDER(server_name2);
    SessionInfo info2 = CreateTestSession(server_name2, common::UTCTimeMsec() / 1000, 3600, false);
    EXPECT_TRUE(cache.StoreSession(session_der2, info2));

    // Should not be valid for 0-RTT
    EXPECT_FALSE(cache.HasValidSessionFor0RTT(server_name2));
}

// Test session expiration
TEST_F(SessionCacheTest, SessionExpiration) {
    SessionCache& cache = SessionCache::Instance();
    EXPECT_TRUE(cache.Init(test_cache_dir_.string()));

    std::string server_name = "example.com";
    std::string session_der = CreateTestSessionDER(server_name);

    // Create session with very short timeout (1 second)
    SessionInfo info = CreateTestSession(server_name, common::UTCTimeMsec() / 1000, 1, true);
    EXPECT_TRUE(cache.StoreSession(session_der, info));

    // Should be valid initially
    EXPECT_TRUE(cache.HasValidSessionFor0RTT(server_name));

    // Wait for expiration
    WaitForTime(2);

    // Should be expired now
    EXPECT_FALSE(cache.HasValidSessionFor0RTT(server_name));

    // Retrieval should fail
    std::string retrieved_der;
    EXPECT_FALSE(cache.GetSession(server_name, retrieved_der));
}

// Test LRU cache eviction
TEST_F(SessionCacheTest, LRUEviction) {
    SessionCache& cache = SessionCache::Instance();
    EXPECT_TRUE(cache.Init(test_cache_dir_.string()));

    // Set small cache size
    cache.SetMaxCacheSize(3);

    // Store 4 sessions (exceeds max size)
    for (int i = 1; i <= 4; ++i) {
        std::string server_name = "example" + std::to_string(i) + ".com";
        std::string session_der = CreateTestSessionDER(server_name);
        SessionInfo info = CreateTestSession(server_name, common::UTCTimeMsec() / 1000, 3600, true);
        EXPECT_TRUE(cache.StoreSession(session_der, info));
    }

    // Should have only 3 sessions (LRU eviction)
    EXPECT_EQ(cache.GetCacheSize(), 3);

    // The first session should be evicted (least recently used)
    EXPECT_FALSE(cache.HasValidSessionFor0RTT("example1.com"));

    // The last 3 sessions should still be there
    EXPECT_TRUE(cache.HasValidSessionFor0RTT("example2.com"));
    EXPECT_TRUE(cache.HasValidSessionFor0RTT("example3.com"));
    EXPECT_TRUE(cache.HasValidSessionFor0RTT("example4.com"));
}

// Test LRU order updates
TEST_F(SessionCacheTest, LRUOrderUpdate) {
    SessionCache& cache = SessionCache::Instance();
    EXPECT_TRUE(cache.Init(test_cache_dir_.string()));

    cache.SetMaxCacheSize(2);

    // Store 2 sessions
    std::string session_der1 = CreateTestSessionDER("example1.com");
    SessionInfo info1 = CreateTestSession("example1.com", common::UTCTimeMsec() / 1000, 3600, true);
    EXPECT_TRUE(cache.StoreSession(session_der1, info1));

    std::string session_der2 = CreateTestSessionDER("example2.com");
    SessionInfo info2 = CreateTestSession("example2.com", common::UTCTimeMsec() / 1000, 3600, true);
    EXPECT_TRUE(cache.StoreSession(session_der2, info2));

    // Access first session (should become most recently used)
    std::string retrieved_der;
    EXPECT_TRUE(cache.GetSession("example1.com", retrieved_der));

    // Add third session (should evict example2.com, not example1.com)
    std::string session_der3 = CreateTestSessionDER("example3.com");
    SessionInfo info3 = CreateTestSession("example3.com", common::UTCTimeMsec() / 1000, 3600, true);
    EXPECT_TRUE(cache.StoreSession(session_der3, info3));

    // example2.com should be evicted, example1.com and example3.com should remain
    EXPECT_FALSE(cache.HasValidSessionFor0RTT("example2.com"));
    EXPECT_TRUE(cache.HasValidSessionFor0RTT("example1.com"));
    EXPECT_TRUE(cache.HasValidSessionFor0RTT("example3.com"));
}

// Test cache persistence across restarts
TEST_F(SessionCacheTest, CachePersistence) {
    {
        // First instance
        SessionCache& cache1 = SessionCache::Instance();
        EXPECT_TRUE(cache1.Init(test_cache_dir_.string()));

        std::string server_name = "example.com";
        std::string session_der = CreateTestSessionDER(server_name);
        SessionInfo info = CreateTestSession(server_name, common::UTCTimeMsec() / 1000, 3600, true);

        EXPECT_TRUE(cache1.StoreSession(session_der, info));
        EXPECT_EQ(cache1.GetCacheSize(), 1);
    }

    {
        // Second instance (simulates restart)
        SessionCache& cache2 = SessionCache::Instance();
        EXPECT_TRUE(cache2.Init(test_cache_dir_.string()));

        // Should load session from disk
        EXPECT_EQ(cache2.GetCacheSize(), 1);
        EXPECT_TRUE(cache2.HasValidSessionFor0RTT("example.com"));

        std::string retrieved_der;
        EXPECT_TRUE(cache2.GetSession("example.com", retrieved_der));
        EXPECT_EQ(retrieved_der, CreateTestSessionDER("example.com"));
    }
}

// Test cache clearing
TEST_F(SessionCacheTest, CacheClearing) {
    SessionCache& cache = SessionCache::Instance();
    EXPECT_TRUE(cache.Init(test_cache_dir_.string()));

    // Store multiple sessions
    for (int i = 1; i <= 3; ++i) {
        std::string server_name = "example" + std::to_string(i) + ".com";
        std::string session_der = CreateTestSessionDER(server_name);
        SessionInfo info = CreateTestSession(server_name, common::UTCTimeMsec() / 1000, 3600, true);
        EXPECT_TRUE(cache.StoreSession(session_der, info));
    }

    EXPECT_EQ(cache.GetCacheSize(), 3);

    // Clear cache
    cache.Clear();

    EXPECT_EQ(cache.GetCacheSize(), 0);

    // All sessions should be gone
    for (int i = 1; i <= 3; ++i) {
        std::string server_name = "example" + std::to_string(i) + ".com";
        EXPECT_FALSE(cache.HasValidSessionFor0RTT(server_name));
    }
}

// Test lazy cleanup
TEST_F(SessionCacheTest, LazyCleanup) {
    SessionCache& cache = SessionCache::Instance();
    EXPECT_TRUE(cache.Init(test_cache_dir_.string()));

    // Store sessions with different expiration times
    std::string session_der1 = CreateTestSessionDER("example1.com");
    SessionInfo info1 = CreateTestSession("example1.com", common::UTCTimeMsec() / 1000, 1, true);
    EXPECT_TRUE(cache.StoreSession(session_der1, info1));

    std::string session_der2 = CreateTestSessionDER("example2.com");
    SessionInfo info2 = CreateTestSession("example2.com", common::UTCTimeMsec() / 1000, 3600, true);
    EXPECT_TRUE(cache.StoreSession(session_der2, info2));

    EXPECT_EQ(cache.GetCacheSize(), 2);

    // Wait for first session to expire
    WaitForTime(2);

    // Try to get expired session (should be removed from cache)
    std::string retrieved_der;
    EXPECT_FALSE(cache.GetSession("example1.com", retrieved_der));
    EXPECT_TRUE(cache.HasValidSessionFor0RTT("example2.com"));

    // Cache size should be reduced because expired session was removed during access
    EXPECT_EQ(cache.GetCacheSize(), 1);
}

// Test force cleanup
TEST_F(SessionCacheTest, ForceCleanup) {
    SessionCache& cache = SessionCache::Instance();
    EXPECT_TRUE(cache.Init(test_cache_dir_.string()));

    // Store sessions with different expiration times
    std::string session_der1 = CreateTestSessionDER("example1.com");
    SessionInfo info1 = CreateTestSession("example1.com", common::UTCTimeMsec() / 1000, 1, true);
    EXPECT_TRUE(cache.StoreSession(session_der1, info1));

    std::string session_der2 = CreateTestSessionDER("example2.com");
    SessionInfo info2 = CreateTestSession("example2.com", common::UTCTimeMsec() / 1000, 3600, true);
    EXPECT_TRUE(cache.StoreSession(session_der2, info2));

    EXPECT_EQ(cache.GetCacheSize(), 2);

    // Wait for first session to expire
    WaitForTime(2);

    // Force cleanup
    cache.ForceCleanup();

    // Only valid session should remain
    EXPECT_EQ(cache.GetCacheSize(), 1);
    EXPECT_FALSE(cache.HasValidSessionFor0RTT("example1.com"));
    EXPECT_TRUE(cache.HasValidSessionFor0RTT("example2.com"));
}

// Test max cache size changes
TEST_F(SessionCacheTest, MaxCacheSizeChanges) {
    SessionCache& cache = SessionCache::Instance();
    EXPECT_TRUE(cache.Init(test_cache_dir_.string()));

    // Store 3 sessions
    for (int i = 1; i <= 3; ++i) {
        std::string server_name = "example" + std::to_string(i) + ".com";
        std::string session_der = CreateTestSessionDER(server_name);
        SessionInfo info = CreateTestSession(server_name, common::UTCTimeMsec() / 1000, 3600, true);
        EXPECT_TRUE(cache.StoreSession(session_der, info));
    }

    EXPECT_EQ(cache.GetCacheSize(), 3);

    // Reduce max cache size to 1
    cache.SetMaxCacheSize(1);

    // Should evict LRU entries
    EXPECT_EQ(cache.GetCacheSize(), 1);

    // Only the most recently used session should remain
    EXPECT_TRUE(cache.HasValidSessionFor0RTT("example3.com"));
    EXPECT_FALSE(cache.HasValidSessionFor0RTT("example1.com"));
    EXPECT_FALSE(cache.HasValidSessionFor0RTT("example2.com"));
}

// Test safe filename generation
TEST_F(SessionCacheTest, SafeFilenameGeneration) {
    SessionCache& cache = SessionCache::Instance();
    EXPECT_TRUE(cache.Init(test_cache_dir_.string()));

    // Test various server names with unsafe characters
    std::vector<std::string> test_names = {"example.com", "example.com:443", "example.com/path", "example.com\\path",
        "example*.com", "example?.com", "example\".com", "example<.com", "example>.com", "example|.com"};

    for (const auto& server_name : test_names) {
        std::string session_der = CreateTestSessionDER(server_name);
        SessionInfo info = CreateTestSession(server_name, common::UTCTimeMsec() / 1000, 3600, true);

        EXPECT_TRUE(cache.StoreSession(session_der, info));

        // Should be able to retrieve
        std::string retrieved_der;
        EXPECT_TRUE(cache.GetSession(server_name, retrieved_der));
        EXPECT_EQ(retrieved_der, session_der);
    }
}

// Test concurrent access
TEST_F(SessionCacheTest, ConcurrentAccess) {
    SessionCache& cache = SessionCache::Instance();
    EXPECT_TRUE(cache.Init(test_cache_dir_.string()));

    // Set cache size large enough to hold all sessions
    const int num_threads = 10;
    const int sessions_per_thread = 5;
    cache.SetMaxCacheSize(num_threads * sessions_per_thread);

    std::vector<std::thread> threads;

    // Start multiple threads that store and retrieve sessions
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&cache, t, sessions_per_thread]() {
            for (int i = 0; i < sessions_per_thread; ++i) {
                std::string server_name = "thread" + std::to_string(t) + "_example" + std::to_string(i) + ".com";
                std::string session_der = "session_data_for_" + server_name;
                SessionInfo info;
                info.server_name = server_name;
                info.creation_time = common::UTCTimeMsec() / 1000;
                info.timeout = 3600;
                info.early_data_capable = true;

                EXPECT_TRUE(cache.StoreSession(session_der, info));

                // Retrieve immediately
                std::string retrieved_der;
                EXPECT_TRUE(cache.GetSession(server_name, retrieved_der));
                EXPECT_EQ(retrieved_der, session_der);
            }
        });
    }

    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }

    // All sessions should be accessible
    EXPECT_EQ(cache.GetCacheSize(), num_threads * sessions_per_thread);

    for (int t = 0; t < num_threads; ++t) {
        for (int i = 0; i < sessions_per_thread; ++i) {
            std::string server_name = "thread" + std::to_string(t) + "_example" + std::to_string(i) + ".com";
            EXPECT_TRUE(cache.HasValidSessionFor0RTT(server_name));
        }
    }
}

// Test error handling
TEST_F(SessionCacheTest, ErrorHandling) {
    SessionCache& cache = SessionCache::Instance();

    // Test initialization with file instead of directory
    std::filesystem::path test_file = test_cache_dir_ / "test_file";
    std::ofstream file(test_file);
    file << "test content";
    file.close();

    EXPECT_FALSE(cache.Init(test_file.string()));

    // Test with valid directory
    EXPECT_TRUE(cache.Init(test_cache_dir_.string()));

    // Test storing session with empty server name
    std::string session_der = "test_data";
    SessionInfo info = CreateTestSession("", common::UTCTimeMsec() / 1000, 3600, true);
    EXPECT_TRUE(cache.StoreSession(session_der, info));

    // Test retrieving non-existent session
    std::string retrieved_der;
    EXPECT_FALSE(cache.GetSession("non_existent.com", retrieved_der));
}

// Test session info validation
TEST_F(SessionCacheTest, SessionInfoValidation) {
    SessionCache& cache = SessionCache::Instance();
    EXPECT_TRUE(cache.Init(test_cache_dir_.string()));

    // Test session with zero timeout
    std::string server_name = "example.com";
    std::string session_der = CreateTestSessionDER(server_name);
    SessionInfo info = CreateTestSession(server_name, common::UTCTimeMsec() / 1000, 0, true);

    EXPECT_TRUE(cache.StoreSession(session_der, info));

    // Should be immediately expired
    EXPECT_FALSE(cache.HasValidSessionFor0RTT(server_name));

    // Test session with very long timeout
    std::string server_name2 = "example2.com";
    std::string session_der2 = CreateTestSessionDER(server_name2);
    SessionInfo info2 = CreateTestSession(server_name2, common::UTCTimeMsec() / 1000, UINT32_MAX, true);

    EXPECT_TRUE(cache.StoreSession(session_der2, info2));

    // Should be valid
    EXPECT_TRUE(cache.HasValidSessionFor0RTT(server_name2));
}

}  // namespace quic
}  // namespace quicx
