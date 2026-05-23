#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <fstream>
#include <filesystem>


#include "common/util/time.h"
#include "quic/connection/session_cache.h"

namespace quicx {
namespace quic {

class SessionCacheTest : public ::testing::Test {
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
    SessionInfo CreateTestSession(const std::string& server_name, uint64_t creation_time, 
                                 uint32_t timeout, bool early_data_capable) {
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
    void WaitForTime(uint32_t seconds) {
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
    }

    std::filesystem::path test_cache_dir_;
};

// Test basic initialization
TEST_F(SessionCacheTest, BasicInitialization) {
    SessionCache& cache = SessionCache::Instance();
    
    EXPECT_TRUE(cache.Init(test_cache_dir_.string()));
    EXPECT_EQ(cache.GetCacheSize(), 0);
    EXPECT_EQ(cache.GetMaxCacheSize(), 100); // Default value
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
    std::vector<std::string> test_names = {
        "example.com",
        "example.com:443",
        "example.com/path",
        "example.com\\path",
        "example*.com",
        "example?.com",
        "example\".com",
        "example<.com",
        "example>.com",
        "example|.com"
    };
    
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

// =============================================================================
// Bug #23 regression: GetSessionWithInfo must faithfully return remembered
// transport params (RFC 9000 §7.4.1) for 0-RTT, including when the on-disk
// session file race-conditions with a same-process retrieval.
//
// Background:
//   ClientConnection::Dial() loads cached_session_info via GetSessionWithInfo()
//   and pre-initializes SendFlowController + transport_param_ so MakeStream()
//   can produce a STREAM frame in the 0-RTT first flight before the server's
//   real transport params arrive. If has_transport_params/peer_initial_max_
//   stream_data_bidi_remote_ come back as 0, the new SendStream is created
//   with peer_data_limit_=0, TrySendData() returns kFlowControlBlocked
//   forever, and the GET request never makes it onto the wire. The server
//   sees only Initial CRYPTO + post-handshake NewConnectionID, then idle-
//   times out at 30s (interop "First connection failed (exit 1)").
// =============================================================================

// Helper: build a fully-populated SessionInfo (all 8 TP fields non-zero)
// matching what TLSClientConnection::OnNewSession + ConnectionClient set
// in production after a successful handshake.
static SessionInfo CreateFullTPSession(const std::string& server_name,
                                       uint64_t creation_time,
                                       uint32_t timeout) {
    SessionInfo info;
    info.server_name = server_name;
    info.creation_time = creation_time;
    info.timeout = timeout;
    info.early_data_capable = true;
    info.has_transport_params = true;
    info.initial_max_data = 786432;
    info.initial_max_streams_bidi = 100;
    info.initial_max_streams_uni = 100;
    info.initial_max_stream_data_bidi_local = 524288;
    info.initial_max_stream_data_bidi_remote = 524288;
    info.initial_max_stream_data_uni = 524288;
    info.active_connection_id_limit = 4;
    return info;
}

// Bug #23: GetSessionWithInfo must round-trip every remembered TP field.
// Before the fix, GetSessionWithInfo synthesized out_info from a fresh
// disk Deserialize(); a corrupt/short on-disk file (or any field-zeroed
// SessionInfo on disk) would silently produce has_transport_params=false
// and zeroed peer_initial_max_stream_data_*, which would break 0-RTT.
TEST_F(SessionCacheTest, GetSessionWithInfoReturnsAllRememberedTransportParams) {
    SessionCache& cache = SessionCache::Instance();
    ASSERT_TRUE(cache.Init(test_cache_dir_.string()));

    const std::string server_name = "server4";
    const std::string session_der = CreateTestSessionDER(server_name);
    SessionInfo stored = CreateFullTPSession(server_name,
                                             common::UTCTimeMsec() / 1000, 3600);
    ASSERT_TRUE(cache.StoreSession(session_der, stored));

    std::string got_der;
    SessionInfo got;
    ASSERT_TRUE(cache.GetSessionWithInfo(server_name, got_der, got));

    EXPECT_EQ(got_der, session_der);
    EXPECT_EQ(got.server_name, stored.server_name);
    EXPECT_TRUE(got.early_data_capable);
    // Critical: every TP field that 0-RTT pre-merge depends on.
    EXPECT_TRUE(got.has_transport_params);
    EXPECT_EQ(got.initial_max_data, stored.initial_max_data);
    EXPECT_EQ(got.initial_max_streams_bidi, stored.initial_max_streams_bidi);
    EXPECT_EQ(got.initial_max_streams_uni, stored.initial_max_streams_uni);
    EXPECT_EQ(got.initial_max_stream_data_bidi_local,
              stored.initial_max_stream_data_bidi_local);
    EXPECT_EQ(got.initial_max_stream_data_bidi_remote,
              stored.initial_max_stream_data_bidi_remote);
    EXPECT_EQ(got.initial_max_stream_data_uni, stored.initial_max_stream_data_uni);
    EXPECT_EQ(got.active_connection_id_limit, stored.active_connection_id_limit);
}

// Bug #23 (race fallback): even if the on-disk SessionInfo trailer is
// corrupted/truncated (which models the observed disk-read race in
// interop tests where conn2's GetSessionWithInfo() fires before conn1's
// ofstream destructor has fully flushed the version-2 trailer), the
// in-memory SessionInfo populated atomically by StoreSession() must be
// preferred. Concretely: we corrupt the trailer to set has_transport_
// params=false on disk, then verify GetSessionWithInfo() still returns
// the correct (non-zero) TP fields from memory.
TEST_F(SessionCacheTest,
       GetSessionWithInfoPrefersMemoryWhenDiskTrailerIsCorrupt) {
    SessionCache& cache = SessionCache::Instance();
    ASSERT_TRUE(cache.Init(test_cache_dir_.string()));

    const std::string server_name = "server4";
    const std::string session_der = CreateTestSessionDER(server_name);
    SessionInfo stored = CreateFullTPSession(server_name,
                                             common::UTCTimeMsec() / 1000, 3600);
    ASSERT_TRUE(cache.StoreSession(session_der, stored));

    // Locate the on-disk session file and overwrite its v2 trailer with
    // zeroes (simulating a partial flush where everything through
    // session_der has hit disk but the TP trailer is still in the kernel
    // buffer / page cache). The exact layout (matches Serialize()):
    //   [0..3]   magic     "SESS"
    //   [4..7]   version   = 2
    //   [8..15]  creation_time
    //   [16..19] timeout
    //   [20]     early_data_capable
    //   [21..24] server_name_len
    //   [...]    server_name
    //   [...]    session_der_len (4 bytes)
    //   [...]    session_der
    //   <-- v2 trailer (33 bytes total: 1 byte bool + 8x uint32_t)
    SerializedSessionData fp_helper;
    std::string filepath = fp_helper.GetFilePath(test_cache_dir_.string(), server_name);
    ASSERT_TRUE(std::filesystem::exists(filepath));
    auto file_size = std::filesystem::file_size(filepath);
    constexpr size_t kV2TrailerSize = sizeof(bool) + 7 * sizeof(uint32_t);
    ASSERT_GT(file_size, kV2TrailerSize);

    // Truncate the file to exactly drop the v2 trailer, then re-Deserialize
    // would observe partial data. We instead overwrite the trailer with
    // zeroes (a stronger model: file is fully readable but stale/incoherent).
    {
        std::fstream f(filepath, std::ios::in | std::ios::out | std::ios::binary);
        ASSERT_TRUE(f.is_open());
        f.seekp(static_cast<std::streamoff>(file_size - kV2TrailerSize), std::ios::beg);
        std::vector<char> zeros(kV2TrailerSize, 0);
        f.write(zeros.data(), zeros.size());
        ASSERT_TRUE(f.good());
    }

    // Now GetSessionWithInfo must still produce the correct TP fields,
    // because the in-memory SessionInfo from StoreSession() is the
    // source of truth. (Pre-fix this would have returned has_transport_
    // params=false and zeroed TP fields.)
    std::string got_der;
    SessionInfo got;
    ASSERT_TRUE(cache.GetSessionWithInfo(server_name, got_der, got));
    EXPECT_EQ(got_der, session_der);
    EXPECT_TRUE(got.has_transport_params)
        << "GetSessionWithInfo should fall back to in-memory SessionInfo "
           "when the on-disk trailer is incoherent (Bug #23 race).";
    EXPECT_EQ(got.initial_max_stream_data_bidi_remote,
              stored.initial_max_stream_data_bidi_remote)
        << "peer_initial_max_stream_data_bidi_remote_ MUST be non-zero in "
           "0-RTT pre-merge or SendStream::TrySendData would block forever.";
    EXPECT_EQ(got.initial_max_stream_data_bidi_local,
              stored.initial_max_stream_data_bidi_local);
    EXPECT_EQ(got.initial_max_data, stored.initial_max_data);
}

// Bug #23 (cross-process restart path): when SessionCache is reinitialized
// from disk only (no prior StoreSession in this process — i.e. the in-memory
// info comes from LoadSessionsFromCache), GetSessionWithInfo must still
// return the full TP set. This guards the "real" cross-process resumption
// flow (e.g. quicX client restart) and proves the disk format itself is
// lossless for v2 sessions.
//
// We deliberately DO NOT use Reset() between phases (Reset() also removes
// session files from disk, which would defeat the purpose of testing
// cross-restart loading). Instead we use a second test directory and
// hand-serialize a v2 session file via SerializedSessionData to mimic what
// a previous process left on disk, then Init() picks it up via
// LoadSessionsFromCache().
TEST_F(SessionCacheTest, GetSessionWithInfoAfterRestartPreservesAllTP) {
    const std::string server_name = "server4";
    const std::string session_der = CreateTestSessionDER(server_name);
    SessionInfo stored = CreateFullTPSession(server_name,
                                             common::UTCTimeMsec() / 1000, 3600);

    // Phase 1 (simulates a previous process): write a real session file
    // to disk via SerializedSessionData::Serialize so the on-disk layout
    // is exactly what production writes.
    {
        SerializedSessionData data;
        data.info = stored;
        data.session_der = session_der;
        std::string filepath = data.GetFilePath(test_cache_dir_.string(), server_name);
        std::ofstream file(filepath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(file.is_open());
        ASSERT_TRUE(data.Serialize(file));
        file.flush();
        file.close();
        ASSERT_TRUE(std::filesystem::exists(filepath));
        ASSERT_GT(std::filesystem::file_size(filepath), 0u);
    }

    // Phase 2 (new process boot): Init() the cache and verify
    // GetSessionWithInfo returns full TP from disk.
    SessionCache& cache = SessionCache::Instance();
    ASSERT_TRUE(cache.Init(test_cache_dir_.string()));
    ASSERT_EQ(cache.GetCacheSize(), 1u)
        << "LoadSessionsFromCache should pick up the on-disk v2 session.";

    std::string got_der;
    SessionInfo got;
    ASSERT_TRUE(cache.GetSessionWithInfo(server_name, got_der, got));
    EXPECT_EQ(got_der, session_der);
    EXPECT_TRUE(got.has_transport_params);
    EXPECT_EQ(got.initial_max_data, stored.initial_max_data);
    EXPECT_EQ(got.initial_max_streams_bidi, stored.initial_max_streams_bidi);
    EXPECT_EQ(got.initial_max_streams_uni, stored.initial_max_streams_uni);
    EXPECT_EQ(got.initial_max_stream_data_bidi_local,
              stored.initial_max_stream_data_bidi_local);
    EXPECT_EQ(got.initial_max_stream_data_bidi_remote,
              stored.initial_max_stream_data_bidi_remote);
    EXPECT_EQ(got.initial_max_stream_data_uni,
              stored.initial_max_stream_data_uni);
    EXPECT_EQ(got.active_connection_id_limit,
              stored.active_connection_id_limit);
}

// Bug #23 (in-process two-connection sequence): conn1 stores a session via
// StoreSession; immediately afterwards conn2 calls GetSessionWithInfo. This
// is the exact production sequence in interop_client (HqInteropClient
// client; client.Init(); ... client.Shutdown(); HqInteropClient client2;
// client2.Init();). Hammer this round-trip many times to catch any
// remaining flakiness in TP propagation.
TEST_F(SessionCacheTest, RepeatedStoreThenGetSessionWithInfoIsStable) {
    SessionCache& cache = SessionCache::Instance();
    ASSERT_TRUE(cache.Init(test_cache_dir_.string()));

    const std::string server_name = "server4";
    const std::string session_der = CreateTestSessionDER(server_name);

    constexpr int kIterations = 50;
    for (int i = 0; i < kIterations; ++i) {
        SessionInfo stored = CreateFullTPSession(server_name,
                                                 common::UTCTimeMsec() / 1000, 3600);
        // Vary fields to ensure each iteration's data is observable.
        stored.initial_max_data = 786432 + i;
        stored.initial_max_stream_data_bidi_remote = 524288 + i;
        ASSERT_TRUE(cache.StoreSession(session_der, stored));

        std::string got_der;
        SessionInfo got;
        ASSERT_TRUE(cache.GetSessionWithInfo(server_name, got_der, got))
            << "iteration " << i;
        ASSERT_TRUE(got.has_transport_params) << "iteration " << i;
        EXPECT_EQ(got.initial_max_data, stored.initial_max_data) << "iteration " << i;
        EXPECT_EQ(got.initial_max_stream_data_bidi_remote,
                  stored.initial_max_stream_data_bidi_remote)
            << "iteration " << i;
        EXPECT_EQ(got.initial_max_stream_data_bidi_local,
                  stored.initial_max_stream_data_bidi_local)
            << "iteration " << i;
    }
}

// Bug #23 (defense-in-depth): even with early_data_capable=false (no
// 0-RTT), if has_transport_params=true the caller still gets correct TP
// values. Guards against accidentally tying TP retrieval to
// early_data_capable.
TEST_F(SessionCacheTest, GetSessionWithInfoReturnsTPEvenWithoutEarlyData) {
    SessionCache& cache = SessionCache::Instance();
    ASSERT_TRUE(cache.Init(test_cache_dir_.string()));

    const std::string server_name = "server4";
    SessionInfo stored = CreateFullTPSession(server_name,
                                             common::UTCTimeMsec() / 1000, 3600);
    stored.early_data_capable = false; // pure 1-RTT resumption, but still has TP

    ASSERT_TRUE(cache.StoreSession(CreateTestSessionDER(server_name), stored));

    std::string got_der;
    SessionInfo got;
    ASSERT_TRUE(cache.GetSessionWithInfo(server_name, got_der, got));
    EXPECT_FALSE(got.early_data_capable);
    EXPECT_TRUE(got.has_transport_params);
    EXPECT_EQ(got.initial_max_stream_data_bidi_remote,
              stored.initial_max_stream_data_bidi_remote);
}

// =============================================================================
// Bug #23 (canonical reproducer): two-callback overwrite race.
//
// In production the client receives a NewSessionTicket (NST) post-handshake
// via BoringSSL's NewSessionCallback (-> TLSClientConnection::OnNewSession).
// That hook does NOT have access to the connection's remembered transport
// parameters, so it writes a SessionInfo with has_transport_params=false.
// Concurrently / earlier, ClientConnection::HandleHandshakeDoneFrame() fills
// a *full* SessionInfo (has_transport_params=true + 8 TP fields) via
// ExportSession() and StoreSession()s that.
//
// If OnNewSession fires AFTER HandleHandshakeDoneFrame (the common case
// observed in interop), it OVERWRITES sessions_cache_[server] with a stale,
// TP-less SessionInfo. Conn 2's GetSessionWithInfo() then returns
// has_transport_params=false, the 0-RTT pre-merge in
// ClientConnection::Dial() is skipped, peer_initial_max_stream_data_bidi_
// remote_ stays 0, and the freshly-created SendStream's first STREAM frame
// is permanently kFlowControlBlocked. The server sees only Initial+ACK,
// then idle-times out (the 41.7s "First connection failed" interop FAIL).
//
// This test models that race deterministically: store full TP first, then
// store again with the same server_name but TP-less SessionInfo (mimicking
// OnNewSession), and verify the cache no longer leaks the previously
// known TP. Once StoreSession is fixed (e.g., merge-instead-of-replace
// when the new info has no TP and the old entry does), this test will
// document the contract.
// =============================================================================
TEST_F(SessionCacheTest, NSTOverwriteMustNotClobberRememberedTP) {
    SessionCache& cache = SessionCache::Instance();
    ASSERT_TRUE(cache.Init(test_cache_dir_.string()));

    const std::string server_name = "server4";

    // Phase 1: HandleHandshakeDoneFrame writes full info (with TP).
    SessionInfo full = CreateFullTPSession(server_name,
                                           common::UTCTimeMsec() / 1000, 3600);
    const std::string der1 = "handshake_done_session_der_for_" + server_name;
    ASSERT_TRUE(cache.StoreSession(der1, full));

    // Phase 2: NewSessionCallback -> OnNewSession writes TP-less info.
    // It carries the NST DER (the only one usable for 0-RTT) but does NOT
    // know the remembered transport params.
    SessionInfo nst_only;
    nst_only.server_name = server_name;
    nst_only.creation_time = common::UTCTimeMsec() / 1000;
    nst_only.timeout = 3600;
    nst_only.early_data_capable = true;     // BoringSSL's NST advertises 0-RTT
    nst_only.has_transport_params = false;  // <-- the bug: TP-less
    const std::string nst_der = "nst_session_der_for_" + server_name;
    ASSERT_TRUE(cache.StoreSession(nst_der, nst_only));

    // Phase 3: conn2 reads back. With the bug the cached info is the TP-less
    // one and 0-RTT pre-merge would be skipped. The contract this test
    // enforces: after the second StoreSession, the cache MUST still expose
    // the previously-known TP fields. (The fix may either be: (a) merge
    // info on Store when new info has no TP and old has, or (b) push the
    // burden to GetSessionWithInfo to retain the last known good TP.)
    std::string got_der;
    SessionInfo got;
    ASSERT_TRUE(cache.GetSessionWithInfo(server_name, got_der, got));

    // The DER must come from the second store (NST is required for 0-RTT).
    EXPECT_EQ(got_der, nst_der);
    EXPECT_TRUE(got.early_data_capable);

    // The TP must still be the ones we learned from the prior handshake.
    // (Pre-fix: this fails — has_transport_params=false, all TP fields=0.)
    EXPECT_TRUE(got.has_transport_params)
        << "OnNewSession's TP-less StoreSession must NOT clobber the "
           "previously stored remembered transport parameters (Bug #23).";
    EXPECT_EQ(got.initial_max_data, full.initial_max_data);
    EXPECT_EQ(got.initial_max_streams_bidi, full.initial_max_streams_bidi);
    EXPECT_EQ(got.initial_max_streams_uni, full.initial_max_streams_uni);
    EXPECT_EQ(got.initial_max_stream_data_bidi_local,
              full.initial_max_stream_data_bidi_local);
    EXPECT_EQ(got.initial_max_stream_data_bidi_remote,
              full.initial_max_stream_data_bidi_remote);
    EXPECT_EQ(got.initial_max_stream_data_uni,
              full.initial_max_stream_data_uni);
    EXPECT_EQ(got.active_connection_id_limit,
              full.active_connection_id_limit);
}

// Bug #23 (reverse order): NST may also arrive BEFORE HandleHandshakeDone-
// Frame is processed (if quic-go packs NST + HANDSHAKE_DONE in the same
// 1-RTT packet and the parser order varies). In that case the second
// store carries full TP and the cache should converge to the full info.
// This test asserts the symmetric behavior to NSTOverwriteMustNotClobberRememberedTP.
TEST_F(SessionCacheTest, HandshakeDoneAfterNSTYieldsFullTP) {
    SessionCache& cache = SessionCache::Instance();
    ASSERT_TRUE(cache.Init(test_cache_dir_.string()));

    const std::string server_name = "server4";

    // Phase 1: OnNewSession arrives first (TP-less, NST DER).
    SessionInfo nst_only;
    nst_only.server_name = server_name;
    nst_only.creation_time = common::UTCTimeMsec() / 1000;
    nst_only.timeout = 3600;
    nst_only.early_data_capable = true;
    nst_only.has_transport_params = false;
    const std::string nst_der = "nst_session_der_for_" + server_name;
    ASSERT_TRUE(cache.StoreSession(nst_der, nst_only));

    // Phase 2: HandleHandshakeDoneFrame fills full TP. ExportSession at
    // this point returns the NST DER (because OnNewSession already ran),
    // so the second store carries identical DER + full TP.
    SessionInfo full = CreateFullTPSession(server_name,
                                           common::UTCTimeMsec() / 1000, 3600);
    ASSERT_TRUE(cache.StoreSession(nst_der, full));

    std::string got_der;
    SessionInfo got;
    ASSERT_TRUE(cache.GetSessionWithInfo(server_name, got_der, got));
    EXPECT_EQ(got_der, nst_der);
    EXPECT_TRUE(got.has_transport_params);
    EXPECT_EQ(got.initial_max_stream_data_bidi_remote,
              full.initial_max_stream_data_bidi_remote);
}

} // namespace quic
} // namespace quicx
