#include "quic/connection/retry_token_manager.h"
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <chrono>
#include <cstring>
#include <ctime>
#include "common/log/log.h"

namespace quicx {
namespace quic {

RetryTokenManager::RetryTokenManager():
    last_rotation_time_(std::time(nullptr)) {
    GenerateRandomSecret();
}

std::string RetryTokenManager::GenerateToken(const common::Address& client_addr, const ConnectionID& original_dcid) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check if need to rotate
    uint64_t now = std::time(nullptr);
    if (now - last_rotation_time_ > ROTATION_INTERVAL) {
        RotateSecret();
    }

    // Build data: client_ip || timestamp || dcid
    // Use milliseconds for better precision
    auto now_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    uint64_t timestamp = static_cast<uint64_t>(now_ms);
    std::string data;
    data += client_addr.GetIp();
    data.append((char*)&timestamp, sizeof(timestamp));
    // Serialize ConnectionID: length + id bytes
    uint8_t cid_len = original_dcid.GetLength();
    data.append((char*)&cid_len, sizeof(cid_len));
    data.append((char*)original_dcid.GetID(), cid_len);

    // Compute HMAC
    std::string hmac = ComputeHMAC(data);

    // Build token: timestamp || HMAC
    std::string token;
    token.append((char*)&timestamp, sizeof(timestamp));
    token.append(hmac);

    common::LOG_DEBUG("Generated Retry token for %s, size=%zu", client_addr.GetIp().c_str(), token.size());

    return token;
}

bool RetryTokenManager::ValidateToken(const std::string& token, const common::Address& client_addr,
    const ConnectionID& original_dcid, uint64_t max_age_seconds) {
    // Check token size
    if (token.size() != sizeof(uint64_t) + 32) {
        common::LOG_WARN("Invalid Retry token size: %zu (expected %zu)", token.size(), sizeof(uint64_t) + 32);
        return false;
    }

    // Extract timestamp
    uint64_t timestamp;
    std::memcpy(&timestamp, token.data(), sizeof(timestamp));

    // Check expiration
    auto now_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    uint64_t now = static_cast<uint64_t>(now_ms);

    if (now < timestamp) {
        common::LOG_WARN("Retry token timestamp in future: token=%llu, now=%llu", timestamp, now);
        return false;
    }

    // Convert max_age from seconds to milliseconds
    uint64_t max_age_ms = max_age_seconds * 1000;
    uint64_t age = now - timestamp;
    if (age >= max_age_ms) {
        common::LOG_WARN("Retry token expired: age=%llu ms, max=%llu ms", age, max_age_ms);
        return false;
    }

    // Rebuild data
    std::string data;
    data += client_addr.GetIp();
    data.append((char*)&timestamp, sizeof(timestamp));
    // Serialize ConnectionID: length + id bytes
    uint8_t cid_len = original_dcid.GetLength();
    data.append((char*)&cid_len, sizeof(cid_len));
    data.append((char*)original_dcid.GetID(), cid_len);

    std::lock_guard<std::mutex> lock(mutex_);

    // Try current secret
    std::string expected_hmac = ComputeHMAC(data);
    std::string actual_hmac = token.substr(sizeof(timestamp));

    if (expected_hmac == actual_hmac) {
        common::LOG_DEBUG("Retry token validated successfully (current secret)");
        return true;
    }

    // Try previous secret (during rotation window)
    if (!previous_secret_.empty()) {
        std::string temp = current_secret_;
        current_secret_ = previous_secret_;
        expected_hmac = ComputeHMAC(data);
        current_secret_ = temp;

        if (expected_hmac == actual_hmac) {
            common::LOG_DEBUG("Retry token validated successfully (previous secret)");
            return true;
        }
    }

    common::LOG_WARN("Retry token HMAC validation failed for %s", client_addr.GetIp().c_str());
    return false;
}

void RetryTokenManager::RotateSecret() {
    previous_secret_ = current_secret_;
    GenerateRandomSecret();
    last_rotation_time_ = std::time(nullptr);
    common::LOG_INFO("Retry token secret rotated");
}

std::string RetryTokenManager::ComputeHMAC(const std::string& data) {
    unsigned char hmac[32];
    unsigned int hmac_len;

    HMAC(EVP_sha256(), current_secret_.data(), current_secret_.size(), (unsigned char*)data.data(), data.size(), hmac,
        &hmac_len);

    return std::string((char*)hmac, hmac_len);
}

void RetryTokenManager::GenerateRandomSecret() {
    unsigned char key[SECRET_SIZE];
    RAND_bytes(key, SECRET_SIZE);
    current_secret_.assign((char*)key, SECRET_SIZE);
}

}  // namespace quic
}  // namespace quicx
