#include <cstring>
#include <algorithm>
#include <filesystem>

#include "common/log/log.h"
#include "common/util/time.h"
#include "quic/connection/session_cache.h"

namespace quicx {
namespace quic {

// SerializedSessionData implementation
bool SerializedSessionData::Serialize(std::ofstream& file) const {
    if (!file.is_open()) {
        return false;
    }
    
    // Write magic number for file validation
    const uint32_t magic = 0x53455353; // "SESS"
    file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    
    // Write version
    const uint32_t version = 1;
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    
    // Write SessionInfo
    file.write(reinterpret_cast<const char*>(&info.creation_time), sizeof(info.creation_time));
    file.write(reinterpret_cast<const char*>(&info.timeout), sizeof(info.timeout));
    file.write(reinterpret_cast<const char*>(&info.early_data_capable), sizeof(info.early_data_capable));
    
    // Write server_name length and data
    uint32_t server_name_len = static_cast<uint32_t>(info.server_name.length());
    file.write(reinterpret_cast<const char*>(&server_name_len), sizeof(server_name_len));
    file.write(info.server_name.c_str(), server_name_len);
    
    // Write session_der length and data
    uint32_t session_der_len = static_cast<uint32_t>(session_der.length());
    file.write(reinterpret_cast<const char*>(&session_der_len), sizeof(session_der_len));
    file.write(session_der.c_str(), session_der_len);
    
    return file.good();
}

bool SerializedSessionData::Deserialize(std::ifstream& file) {
    if (!file.is_open()) {
        return false;
    }
    
    // Read and validate magic number
    uint32_t magic;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (magic != 0x53455353) { // "SESS"
        common::LOG_ERROR("Invalid session file magic number: 0x%08x", magic);
        return false;
    }
    
    // Read version
    uint32_t version;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (version != 1) {
        common::LOG_ERROR("Unsupported session file version: %u", version);
        return false;
    }
    
    // Read SessionInfo
    file.read(reinterpret_cast<char*>(&info.creation_time), sizeof(info.creation_time));
    file.read(reinterpret_cast<char*>(&info.timeout), sizeof(info.timeout));
    file.read(reinterpret_cast<char*>(&info.early_data_capable), sizeof(info.early_data_capable));
    
    // Read server_name
    uint32_t server_name_len;
    file.read(reinterpret_cast<char*>(&server_name_len), sizeof(server_name_len));
    if (server_name_len > 1024) { // Sanity check
        common::LOG_ERROR("Server name too long: %u", server_name_len);
        return false;
    }
    info.server_name.resize(server_name_len);
    file.read(&info.server_name[0], server_name_len);
    
    // Read session_der
    uint32_t session_der_len;
    file.read(reinterpret_cast<char*>(&session_der_len), sizeof(session_der_len));
    if (session_der_len > 65536) { // Sanity check
        common::LOG_ERROR("Session DER too long: %u", session_der_len);
        return false;
    }
    session_der.resize(session_der_len);
    file.read(&session_der[0], session_der_len);
    
    return file.good();
}

std::string SerializedSessionData::GetFilePath(const std::string& cache_dir, const std::string& server_name) const {
    std::string safe_name = SessionCache::GenerateSafeFilename(server_name);
    return cache_dir + "/" + safe_name + ".session";
}

// SessionCache implementation
SessionCache::SessionCache():
    enable_session_cache_(false),
    max_cache_size_(100),
    last_cleanup_time_(0) {
}

SessionCache::~SessionCache() {
    // Cleanup is automatic, no explicit cleanup needed
}

bool SessionCache::Init(const std::string& session_cache_path) {
    if (enable_session_cache_) {
        return true;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    
    session_cache_path_ = session_cache_path;

    // Check if cache path exists and is accessible
    if (!std::filesystem::exists(session_cache_path_)) {
        common::LOG_ERROR("Session cache path does not exist: %s", session_cache_path_.c_str());
         // Create cache directory if it doesn't exist
        try {
            std::filesystem::create_directories(session_cache_path_);
        } catch (const std::exception& e) {
            common::LOG_ERROR("Failed to create session cache directory: %s", e.what());
            return false;
        }
    }

    if (!std::filesystem::is_directory(session_cache_path_)) {
        common::LOG_ERROR("Session cache path is not a directory: %s", session_cache_path_.c_str());
        return false;
    }

    // Check if we have write permissions
    try {
        std::filesystem::path test_file = session_cache_path_ + "/.write_test";
        std::ofstream test(test_file);
        if (!test.is_open()) {
            common::LOG_ERROR("Cannot write to session cache directory: %s", session_cache_path_.c_str());
            return false;
        }
        test.close();
        std::filesystem::remove(test_file);

    } catch (const std::exception& e) {
        common::LOG_ERROR("Failed to verify write permissions on cache directory: %s", e.what());
        return false;
    }
   
    // Load existing sessions from disk
    if (!LoadSessionsFromCache()) {
        common::LOG_WARN("Failed to load sessions from cache, starting with empty cache");
    }
    
    enable_session_cache_ = true;
    last_cleanup_time_ = common::UTCTimeMsec() / 1000;
    
    common::LOG_INFO("SessionCache initialized with path: %s, loaded %zu sessions", 
                    session_cache_path_.c_str(), sessions_cache_.size());
    
    return true;
}

bool SessionCache::StoreSession(const std::string& session_der, const SessionInfo& session_info) {
    if (!enable_session_cache_) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check lazy cleanup
    CheckLazyCleanup();
    
    // Create serialized data
    SerializedSessionData data;
    data.info = session_info;
    data.session_der = session_der;
    
    // Save to disk
    if (!SaveSessionToFile(session_info.server_name, data)) {
        common::LOG_ERROR("Failed to save session to disk for %s", session_info.server_name.c_str());
        return false;
    }
    
    // Store in memory (only SessionInfo, session_der is on disk)
    sessions_cache_[session_info.server_name] = session_info;
    
    // Update LRU order (move to front as most recently used)
    UpdateLRUOrder(session_info.server_name);
    
    // Check if we need to evict entries due to cache size limit (after adding new session)
    EvictLRUEntries();
    
    common::LOG_DEBUG("Stored session for %s, timeout: %u, early_data_capable: %d, cache size: %zu", 
                     session_info.server_name.c_str(), session_info.timeout, session_info.early_data_capable, sessions_cache_.size());
    
    return true;
}

bool SessionCache::GetSession(const std::string& server_name, std::string& out_session_der) {
    if (!enable_session_cache_) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check lazy cleanup
    CheckLazyCleanup();
    
    auto it = sessions_cache_.find(server_name);
    if (it == sessions_cache_.end()) {
        return false;
    }
    
    const SessionInfo& info = it->second;
    if (!IsSessionValid(info)) {
        // Remove expired session
        sessions_cache_.erase(it);
        RemoveSessionFile(server_name);
        RemoveLRUEntry(server_name);
        common::LOG_DEBUG("Session for %s expired, removed from cache", server_name.c_str());
        return false;
    }
    
    // Load session_der from disk
    if (!LoadSessionFromFile(server_name, out_session_der)) {
        common::LOG_ERROR("Failed to load session from disk for %s", server_name.c_str());
        // Remove corrupted entry
        sessions_cache_.erase(it);
        RemoveSessionFile(server_name);
        RemoveLRUEntry(server_name);
        return false;
    }
    
    // Update LRU order (move to front as most recently used)
    UpdateLRUOrder(server_name);
    
    common::LOG_DEBUG("Retrieved valid session for %s, remaining lifetime: %u seconds", 
                     server_name.c_str(), GetSessionRemainingLifetime(info));
    return true;
}

bool SessionCache::HasValidSessionFor0RTT(const std::string& server_name) {
    if (!enable_session_cache_) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check lazy cleanup
    CheckLazyCleanup();
    
    auto it = sessions_cache_.find(server_name);
    if (it == sessions_cache_.end()) {
        return false;
    }
    
    const SessionInfo& info = it->second;
    bool can_use = CanUseSessionFor0RTT(info);
    
    if (!can_use) {
        common::LOG_DEBUG("Session for %s cannot be used for 0-RTT (expired: %d, early_data_capable: %d)", 
                         server_name.c_str(), !IsSessionValid(info), info.early_data_capable);
    } else {
        common::LOG_DEBUG("Session for %s can be used for 0-RTT, remaining lifetime: %u seconds", 
                         server_name.c_str(), GetSessionRemainingLifetime(info));
    }
    
    return can_use;
}

void SessionCache::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Remove all session files from disk
    for (const auto& pair : sessions_cache_) {
        RemoveSessionFile(pair.first);
    }
    
    // Clear memory cache and LRU structures
    sessions_cache_.clear();
    lru_list_.clear();
    lru_map_.clear();
    
    common::LOG_INFO("Session cache cleared (memory and disk)");
}

size_t SessionCache::GetCacheSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_cache_.size();
}

void SessionCache::ForceCleanup() {
    std::lock_guard<std::mutex> lock(mutex_);
    CleanupExpiredSessions();
}

void SessionCache::CheckLazyCleanup() {
    uint64_t current_time = common::UTCTimeMsec() / 1000;
    if (current_time - last_cleanup_time_ >= CLEANUP_CHECK_INTERVAL) {
        CleanupExpiredSessions();
        last_cleanup_time_ = current_time;
    }
}

bool SessionCache::LoadSessionsFromCache() {
    try {
        for (const auto& entry : std::filesystem::directory_iterator(session_cache_path_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".session") {
                std::string filename = entry.path().stem().string();
                
                // Try to load session info from file
                std::ifstream file(entry.path(), std::ios::binary);
                if (!file.is_open()) {
                    common::LOG_WARN("Failed to open session file: %s", entry.path().c_str());
                    continue;
                }
                
                SerializedSessionData data;
                if (!data.Deserialize(file)) {
                    common::LOG_WARN("Failed to deserialize session file: %s", entry.path().c_str());
                    continue;
                }
                
                // Check if session is still valid
                if (IsSessionValid(data.info)) {
                    sessions_cache_[data.info.server_name] = data.info;
                    // Initialize LRU order for loaded sessions (add to front as most recently used)
                    UpdateLRUOrder(data.info.server_name);
                    common::LOG_DEBUG("Loaded valid session for %s from disk", data.info.server_name.c_str());
                } else {
                    common::LOG_DEBUG("Session for %s expired, removing file", data.info.server_name.c_str());
                    std::filesystem::remove(entry.path());
                }
            }
        }
        
        common::LOG_INFO("Loaded %zu valid sessions from disk cache", sessions_cache_.size());
        return true;
    } catch (const std::exception& e) {
        common::LOG_ERROR("Failed to load sessions from cache: %s", e.what());
        return false;
    }
}

bool SessionCache::LoadSessionFromFile(const std::string& server_name, std::string& out_session_der) {
    SerializedSessionData data;
    std::string filepath = data.GetFilePath(session_cache_path_, server_name);
    
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    if (!data.Deserialize(file)) {
        return false;
    }
    
    out_session_der = data.session_der;
    return true;
}

bool SessionCache::SaveSessionToFile(const std::string& server_name, const SerializedSessionData& data) {
    std::string filepath = data.GetFilePath(session_cache_path_, server_name);
    
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        common::LOG_ERROR("Failed to create session file: %s", filepath.c_str());
        return false;
    }
    
    if (!data.Serialize(file)) {
        common::LOG_ERROR("Failed to serialize session data to file: %s", filepath.c_str());
        return false;
    }
    
    return true;
}

void SessionCache::CleanupExpiredSessions() {
    size_t before_size = sessions_cache_.size();
    auto it = sessions_cache_.begin();
    
    while (it != sessions_cache_.end()) {
        if (!IsSessionValid(it->second)) {
            common::LOG_DEBUG("Removing expired session for %s", it->first.c_str());
            RemoveSessionFile(it->first);
            RemoveLRUEntry(it->first);
            it = sessions_cache_.erase(it);
        } else {
            ++it;
        }
    }
    
    size_t after_size = sessions_cache_.size();
    if (before_size != after_size) {
        common::LOG_INFO("Cleaned up %zu expired sessions, cache size: %zu", 
                        before_size - after_size, after_size);
    }
}

bool SessionCache::RemoveSessionFile(const std::string& server_name) {
    try {
        SerializedSessionData data;
        std::string filepath = data.GetFilePath(session_cache_path_, server_name);
        return std::filesystem::remove(filepath);
    } catch (const std::exception& e) {
        common::LOG_ERROR("Failed to remove session file for %s: %s", server_name.c_str(), e.what());
        return false;
    }
}

std::string SessionCache::GenerateSafeFilename(const std::string& server_name) {
    std::string safe_name = server_name;
    
    // Define unsafe characters for filesystem
    const char unsafe_chars[] = "/\\:*?\"<>|";
    for (char c : unsafe_chars) {
        std::replace(safe_name.begin(), safe_name.end(), c, '_');
    }
    
    // Truncate if too long
    const size_t max_filename_length = 255;
    const char* session_file_extension = ".session";
    if (safe_name.length() > max_filename_length - strlen(session_file_extension)) {
        safe_name = safe_name.substr(0, max_filename_length - strlen(session_file_extension));
    }
    
    return safe_name;
}

bool SessionCache::SessionFileExists(const std::string& server_name) const {
    try {
        SerializedSessionData data;
        std::string filepath = data.GetFilePath(session_cache_path_, server_name);
        return std::filesystem::exists(filepath);
    } catch (const std::exception& e) {
        common::LOG_ERROR("Failed to check session file existence for %s: %s", server_name.c_str(), e.what());
        return false;
    }
}

void SessionCache::SetMaxCacheSize(uint32_t max_size) {
    std::lock_guard<std::mutex> lock(mutex_);
    max_cache_size_ = max_size;
    common::LOG_INFO("Session cache max size set to %u", max_size);
    
    // Evict entries if current size exceeds new limit
    EvictLRUEntries();
}

void SessionCache::UpdateLRUOrder(const std::string& server_name) {
    // Remove from current position if exists
    auto lru_it = lru_map_.find(server_name);
    if (lru_it != lru_map_.end()) {
        lru_list_.erase(lru_it->second);
    }
    
    // Add to front (most recently used)
    lru_list_.push_front(server_name);
    lru_map_[server_name] = lru_list_.begin();
}

void SessionCache::EvictLRUEntries() {
    while (sessions_cache_.size() > max_cache_size_) {
        if (lru_list_.empty()) {
            // This shouldn't happen, but just in case
            common::LOG_ERROR("LRU list is empty but cache size is %zu", sessions_cache_.size());
            break;
        }
        
        // Get least recently used entry (back of list)
        std::string lru_server_name = lru_list_.back();
        
        common::LOG_DEBUG("Evicting LRU session for %s (cache size: %zu, max: %u)", 
                         lru_server_name.c_str(), sessions_cache_.size(), max_cache_size_);
        
        // Remove from cache and disk
        auto cache_it = sessions_cache_.find(lru_server_name);
        if (cache_it != sessions_cache_.end()) {
            RemoveSessionFile(lru_server_name);
            sessions_cache_.erase(cache_it);
        }
        
        // Remove from LRU structures
        RemoveLRUEntry(lru_server_name);
    }
}

void SessionCache::RemoveLRUEntry(const std::string& server_name) {
    auto lru_it = lru_map_.find(server_name);
    if (lru_it != lru_map_.end()) {
        lru_list_.erase(lru_it->second);
        lru_map_.erase(lru_it);
    }
}


bool SessionCache::IsSessionValid(const SessionInfo& info) {
    uint64_t current_time = common::UTCTimeMsec() / 1000; // Convert to seconds
    return (current_time < info.creation_time + info.timeout);
}

bool SessionCache::CanUseSessionFor0RTT(const SessionInfo& info) {
    if (!info.early_data_capable) {
        return false;
    }
    
    uint64_t current_time = common::UTCTimeMsec() / 1000; // Convert to seconds
    return (current_time < info.creation_time + info.timeout);
}

uint32_t SessionCache::GetSessionRemainingLifetime(const SessionInfo& info) {
    uint64_t current_time = common::UTCTimeMsec() / 1000; // Convert to seconds
    if (current_time >= info.creation_time + info.timeout) {
        return 0;
    }
    return static_cast<uint32_t>(info.creation_time + info.timeout - current_time);
}

void SessionCache::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Clear all sessions from disk
    for (const auto& pair : sessions_cache_) {
        RemoveSessionFile(pair.first);
    }
    
    // Reset all internal state
    sessions_cache_.clear();
    lru_list_.clear();
    lru_map_.clear();
    enable_session_cache_ = false;
    max_cache_size_ = 100;
    last_cleanup_time_ = 0;
    session_cache_path_.clear();
    
    common::LOG_INFO("SessionCache reset to initial state");
}

}
}
