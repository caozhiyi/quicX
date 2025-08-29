#ifndef QUIC_CONNECTION_SESSION_CACHE
#define QUIC_CONNECTION_SESSION_CACHE

#include <list>
#include <mutex>
#include <string>
#include <fstream>
#include <unordered_map>
#include "common/util/singleton.h"
#include "quic/crypto/tls/tls_connection_client.h"

namespace quicx {
namespace quic {

// Serialized session data structure for disk storage
struct SerializedSessionData {
    SessionInfo info;
    std::string session_der;
    
    // Serialization methods
    bool Serialize(std::ofstream& file) const;
    bool Deserialize(std::ifstream& file);
    
    // Get file path for this session
    std::string GetFilePath(const std::string& cache_dir, const std::string& server_name) const;
};

class SessionCache:
    public common::Singleton<SessionCache> {
public:
    SessionCache();
    ~SessionCache();
    
    // Initialize session cache with disk storage path
    bool Init(const std::string& session_cache_path);

    // Store a new session (serializes to disk)
    bool StoreSession(const std::string& session_der, const SessionInfo& session_info);
    
    // Retrieve a session for a server (loads from disk if needed)
    bool GetSession(const std::string& server_name, std::string& out_session_der);
    
    // Check if a valid session exists for 0-RTT (with lazy cleanup)
    bool HasValidSessionFor0RTT(const std::string& server_name);
    
    // Clear all sessions (both memory and disk)
    void Clear();
    
    // Get cache statistics
    size_t GetCacheSize() const;
    
    // Force cleanup of expired sessions
    void ForceCleanup();
    
    // Set maximum cache size
    void SetMaxCacheSize(uint32_t max_size);
    
    // Get current cache size
    uint32_t GetMaxCacheSize() const { return max_cache_size_; }
    
    // Static utility method for generating safe filenames
    static std::string GenerateSafeFilename(const std::string& server_name);

    // is enable session cache
    bool IsEnableSessionCache() const { return enable_session_cache_; }
    
    // Reset cache state (for testing purposes)
    void Reset();
    
private:
    // Lazy cleanup check - called before external operations
    void CheckLazyCleanup();
    
    // Load all sessions info from cache directory on startup
    bool LoadSessionsFromCache();
    
    // Load session data from disk file
    bool LoadSessionFromFile(const std::string& server_name, std::string& out_session_der);
    
    // Save session data to disk file
    bool SaveSessionToFile(const std::string& server_name, const SerializedSessionData& data);
    
    // Remove expired sessions from memory and disk
    void CleanupExpiredSessions();
    
    // Remove session file from disk
    bool RemoveSessionFile(const std::string& server_name);
    
        // Check if session file exists
    bool SessionFileExists(const std::string& server_name) const;
    
    // LRU cache management
    void UpdateLRUOrder(const std::string& server_name);
    void EvictLRUEntries();
    void RemoveLRUEntry(const std::string& server_name);

    // Check if session is still valid (not expired)
    bool IsSessionValid(const SessionInfo& info);

    // Check if session can be used for 0-RTT
    bool CanUseSessionFor0RTT(const SessionInfo& info);

    // Get remaining lifetime in seconds
    uint32_t GetSessionRemainingLifetime(const SessionInfo& info);
    
private:
    bool enable_session_cache_;
    uint32_t max_cache_size_;
    mutable std::mutex mutex_;
    std::string session_cache_path_;
    
    // server name -> SessionInfo (session_der stored on disk)
    std::unordered_map<std::string, SessionInfo> sessions_cache_;
    
    // LRU cache management
    std::list<std::string> lru_list_;  // Most recently used at front
    std::unordered_map<std::string, std::list<std::string>::iterator> lru_map_;
    
    // Lazy cleanup tracking
    uint64_t last_cleanup_time_;
    static constexpr uint32_t CLEANUP_CHECK_INTERVAL = 1200; // 20 minutes
};

}
}

#endif // QUIC_CONNECTION_SESSION_CACHE
