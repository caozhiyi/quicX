
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <chrono>
#include <cstring>
#include <ctime>

#include "common/log/log.h"
#include "quic/connection/retry_token_manager.h"

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

    // Use milliseconds for better precision
    auto now_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    uint64_t timestamp = static_cast<uint64_t>(now_ms);

    // Build payload: client_ip || timestamp || cid_len || cid
    std::string payload;
    payload += client_addr.GetIp();
    payload.append((char*)&timestamp, sizeof(timestamp));

    // Serialize ConnectionID: length + id bytes
    uint8_t cid_len = original_dcid.GetLength();
    payload.append((char*)&cid_len, sizeof(cid_len));
    payload.append((char*)original_dcid.GetID(), cid_len);

    // Compute HMAC over payload
    std::string hmac = ComputeHMAC(payload);

    // Build final token: timestamp || cid_len || cid || HMAC
    // We recreate the token structure to allow easy parsing:
    // Token = timestamp (8) + cid_len (1) + cid (N) + HMAC (32)
    std::string token;
    token.append((char*)&timestamp, sizeof(timestamp));
    token.append((char*)&cid_len, sizeof(cid_len));
    token.append((char*)original_dcid.GetID(), cid_len);
    token.append(hmac);

    common::LOG_DEBUG("Generated Retry token for %s, size=%zu", client_addr.GetIp().c_str(), token.size());

    return token;
}

bool RetryTokenManager::ValidateToken(const std::string& token, const common::Address& client_addr,
    ConnectionID& out_original_dcid, uint64_t max_age_seconds) {
    // Minimum size: timestamp(8) + cid_len(1) + HMAC(32) = 41 bytes
    if (token.size() < sizeof(uint64_t) + 1 + 32) {
        common::LOG_WARN("Invalid Retry token size: %zu (too short)", token.size());
        return false;
    }

    // Extract timestamp
    uint64_t timestamp;
    size_t offset = 0;
    std::memcpy(&timestamp, token.data() + offset, sizeof(timestamp));
    offset += sizeof(timestamp);

    // Extract CID Length
    uint8_t cid_len = token[offset];
    offset += 1;

    // Check size consistency considering CID length
    if (token.size() != offset + cid_len + 32) {
        common::LOG_WARN("Invalid Retry token size: %zu (expected %zu for CID len %d)", token.size(),
            offset + cid_len + 32, cid_len);
        return false;
    }

    // Extract Original DCID
    if (cid_len > 0) {
        out_original_dcid = ConnectionID(reinterpret_cast<const uint8_t*>(token.data() + offset), cid_len);
    } else {
        out_original_dcid = ConnectionID();
    }
    offset += cid_len;

    // The rest is HMAC (32 bytes)
    std::string actual_hmac = token.substr(offset);

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

    // Rebuild payload for HMAC verification: client_ip || timestamp || cid_len || cid
    std::string payload;
    payload += client_addr.GetIp();
    payload.append((char*)&timestamp, sizeof(timestamp));
    payload.append((char*)&cid_len, sizeof(cid_len));
    if (cid_len > 0) {
        payload.append((char*)out_original_dcid.GetID(), cid_len);
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // Try current secret
    std::string expected_hmac = ComputeHMAC(payload);
    if (expected_hmac == actual_hmac) {
        common::LOG_DEBUG("Retry token validated successfully (current secret)");
        return true;
    }

    // Try previous secret (during rotation window)
    if (!previous_secret_.empty()) {
        std::string temp = current_secret_;
        current_secret_ = previous_secret_;
        expected_hmac = ComputeHMAC(payload);
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
