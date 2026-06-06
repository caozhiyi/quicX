
#include <chrono>
#include <cstring>

#include "common/log/log.h"
#include "quic/connection/retry_token_manager.h"
#include "quic/crypto/retry_crypto.h"

namespace quicx {
namespace quic {

namespace {

// Big-endian write/read helpers. Used so that token wire format and HMAC
// payload are stable across heterogeneous-architecture clusters (rare in
// practice, but cheap to do right and easy to get wrong).
inline void AppendBE16(std::string& out, uint16_t v) {
    uint8_t buf[2];
    buf[0] = static_cast<uint8_t>((v >> 8) & 0xff);
    buf[1] = static_cast<uint8_t>(v & 0xff);
    out.append(reinterpret_cast<char*>(buf), sizeof(buf));
}

inline void AppendBE64(std::string& out, uint64_t v) {
    uint8_t buf[8];
    for (int i = 0; i < 8; ++i) {
        buf[i] = static_cast<uint8_t>((v >> ((7 - i) * 8)) & 0xff);
    }
    out.append(reinterpret_cast<char*>(buf), sizeof(buf));
}

inline uint64_t ReadBE64(const char* src) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(src);
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v = (v << 8) | static_cast<uint64_t>(p[i]);
    }
    return v;
}

inline uint64_t NowMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

}  // namespace

std::string RetryTokenManager::BuildHmacPayload(const common::Address& client_addr, uint64_t timestamp_ms,
    uint8_t cid_len, const uint8_t* cid_bytes) {
    // Layout: client_ip (string) || client_port_be (2) || timestamp_be (8) || cid_len (1) || cid (N)
    std::string payload;
    payload.reserve(client_addr.GetIp().size() + 2 + 8 + 1 + cid_len);
    payload += client_addr.GetIp();
    AppendBE16(payload, client_addr.GetPort());
    AppendBE64(payload, timestamp_ms);
    payload.append(reinterpret_cast<const char*>(&cid_len), sizeof(cid_len));
    if (cid_len > 0 && cid_bytes != nullptr) {
        payload.append(reinterpret_cast<const char*>(cid_bytes), cid_len);
    }
    return payload;
}

RetryTokenManager::RetryTokenManager():
    last_rotation_time_(std::chrono::steady_clock::now()) {
    GenerateRandomSecret();
}

std::string RetryTokenManager::GenerateToken(const common::Address& client_addr, const ConnectionID& original_dcid) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Rotation cadence is driven by a monotonic clock so wall-clock jumps
    // (NTP step, manual settime) cannot accidentally trigger or skip rotation.
    auto steady_now = std::chrono::steady_clock::now();
    if (steady_now - last_rotation_time_ > kRotationInterval) {
        // RotateSecret() also takes mutex_; call its body inline to avoid
        // re-locking. Keep the public RotateSecret() entry point untouched.
        previous_secret_ = current_secret_;
        GenerateRandomSecret();
        last_rotation_time_ = steady_now;
        LOG_INFO("Retry token secret rotated (auto)");
    }

    // Token timestamp is wall-clock (system_clock ms): the client-visible
    // notion of "age" must track real elapsed time, not server uptime.
    uint64_t timestamp = NowMs();

    uint8_t cid_len = original_dcid.GetLength();
    const uint8_t* cid_bytes = original_dcid.GetID();

    // Build payload (binds token to ip+port+timestamp+cid; big-endian)
    std::string payload = BuildHmacPayload(client_addr, timestamp, cid_len, cid_bytes);

    // Compute HMAC over payload using crypto module
    std::string hmac;
    if (!RetryCrypto::ComputeTokenHMAC(payload, current_secret_, hmac)) {
        LOG_ERROR("Failed to compute token HMAC");
        return "";
    }

    // Wire format: timestamp_be (8) || cid_len (1) || cid (N) || HMAC (32)
    std::string token;
    token.reserve(sizeof(uint64_t) + 1 + cid_len + hmac.size());
    AppendBE64(token, timestamp);
    token.append(reinterpret_cast<const char*>(&cid_len), sizeof(cid_len));
    if (cid_len > 0) {
        token.append(reinterpret_cast<const char*>(cid_bytes), cid_len);
    }
    token.append(hmac);

    LOG_DEBUG("Generated Retry token for %s:%u, size=%zu", client_addr.GetIp().c_str(),
        static_cast<unsigned>(client_addr.GetPort()), token.size());

    return token;
}

bool RetryTokenManager::ValidateToken(const std::string& token, const common::Address& client_addr,
    ConnectionID& out_original_dcid, uint64_t max_age_seconds) {
    // Minimum size: timestamp(8) + cid_len(1) + HMAC(32) = 41 bytes
    if (token.size() < sizeof(uint64_t) + 1 + 32) {
        LOG_WARN("Invalid Retry token size: %zu (too short)", token.size());
        return false;
    }

    // Extract timestamp (big-endian)
    size_t offset = 0;
    uint64_t timestamp = ReadBE64(token.data() + offset);
    offset += sizeof(uint64_t);

    // Extract CID Length
    uint8_t cid_len = static_cast<uint8_t>(token[offset]);
    offset += 1;

    // Check size consistency considering CID length
    if (token.size() != offset + cid_len + 32) {
        LOG_WARN("Invalid Retry token size: %zu (expected %zu for CID len %d)", token.size(),
            offset + cid_len + 32, cid_len);
        return false;
    }

    // Extract Original DCID
    const uint8_t* cid_bytes = nullptr;
    if (cid_len > 0) {
        cid_bytes = reinterpret_cast<const uint8_t*>(token.data() + offset);
        out_original_dcid = ConnectionID(cid_bytes, cid_len);
    } else {
        out_original_dcid = ConnectionID();
    }
    offset += cid_len;

    // The rest is HMAC (32 bytes)
    std::string actual_hmac = token.substr(offset);

    // Check expiration (wall-clock)
    uint64_t now = NowMs();

    if (now < timestamp) {
        // Either the token is from the future or system clock was rolled back.
        // Either way refuse: we cannot bound its age.
        LOG_WARN("Retry token timestamp in future: token=%llu, now=%llu",
            static_cast<unsigned long long>(timestamp), static_cast<unsigned long long>(now));
        return false;
    }

    uint64_t max_age_ms = max_age_seconds * 1000;
    uint64_t age = now - timestamp;
    if (age >= max_age_ms) {
        LOG_WARN("Retry token expired: age=%llu ms, max=%llu ms",
            static_cast<unsigned long long>(age), static_cast<unsigned long long>(max_age_ms));
        return false;
    }

    // Rebuild payload using the same canonical big-endian layout
    std::string payload = BuildHmacPayload(client_addr, timestamp, cid_len, cid_bytes);

    std::lock_guard<std::mutex> lock(mutex_);

    // Try current secret using crypto module
    std::string expected_hmac;
    if (!RetryCrypto::ComputeTokenHMAC(payload, current_secret_, expected_hmac)) {
        LOG_ERROR("Failed to compute HMAC for validation");
        return false;
    }

    if (RetryCrypto::VerifyTokenHMAC(expected_hmac, actual_hmac)) {
        LOG_DEBUG("Retry token validated successfully (current secret)");
        return true;
    }

    // Try previous secret (during rotation window)
    if (!previous_secret_.empty()) {
        std::string prev_hmac;
        if (RetryCrypto::ComputeTokenHMAC(payload, previous_secret_, prev_hmac)) {
            if (RetryCrypto::VerifyTokenHMAC(prev_hmac, actual_hmac)) {
                LOG_DEBUG("Retry token validated successfully (previous secret)");
                return true;
            }
        }
    }

    LOG_WARN("Retry token HMAC validation failed for %s:%u", client_addr.GetIp().c_str(),
        static_cast<unsigned>(client_addr.GetPort()));
    return false;
}

void RetryTokenManager::RotateSecret() {
    std::lock_guard<std::mutex> lock(mutex_);
    previous_secret_ = current_secret_;
    GenerateRandomSecret();
    last_rotation_time_ = std::chrono::steady_clock::now();
    LOG_INFO("Retry token secret rotated");
}

std::string RetryTokenManager::ComputeHMAC(const std::string& data) {
    std::string hmac;
    if (!RetryCrypto::ComputeTokenHMAC(data, current_secret_, hmac)) {
        LOG_ERROR("ComputeHMAC failed");
        return "";
    }
    return hmac;
}

void RetryTokenManager::GenerateRandomSecret() {
    if (!RetryCrypto::GenerateRandomSecret(SECRET_SIZE, current_secret_)) {
        LOG_ERROR("Failed to generate random secret for Retry tokens");
        // Fallback: use empty secret (not secure, but prevents crash)
        current_secret_.clear();
    }
}

}  // namespace quic
}  // namespace quicx
