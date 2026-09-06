#include <cstdlib>
#include <cstring>

#include <openssl/hmac.h>
#include <openssl/mem.h>
#include <openssl/rand.h>

#include "common/log/log.h"

#include "quic/connection/stateless_reset_token_generator.h"

namespace quicx {
namespace quic {

namespace {

constexpr char kKeyEnvVar[] = "QUICX_STATELESS_RESET_KEY";

// Decodes |hex| into raw bytes. Returns false on odd length or any non-hex
// character, so a mistyped key is rejected loudly rather than silently becoming
// a different (weaker) key than the operator intended.
bool HexDecode(const std::string& hex, std::string& out) {
    if (hex.empty() || hex.size() % 2 != 0) {
        return false;
    }
    out.clear();
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        int hi = -1;
        int lo = -1;
        for (int j = 0; j < 2; ++j) {
            const char c = hex[i + j];
            int v;
            if (c >= '0' && c <= '9') {
                v = c - '0';
            } else if (c >= 'a' && c <= 'f') {
                v = c - 'a' + 10;
            } else if (c >= 'A' && c <= 'F') {
                v = c - 'A' + 10;
            } else {
                return false;
            }
            if (j == 0) {
                hi = v;
            } else {
                lo = v;
            }
        }
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return true;
}

}  // namespace

StatelessResetTokenGenerator::StatelessResetTokenGenerator() {
    // Secrets come from the environment only: never a config file committed to a
    // repo, never a command line visible in the process table.
    const char* env_key = std::getenv(kKeyEnvVar);
    if (env_key != nullptr && *env_key != '\0') {
        std::string decoded;
        if (!HexDecode(env_key, decoded)) {
            LOG_ERROR("%s is not valid hex; ignoring it", kKeyEnvVar);
        } else if (decoded.size() < kMinKeyLength) {
            LOG_ERROR(
                "%s is too short (%zu bytes, need >= %zu); ignoring it", kKeyEnvVar, decoded.size(), kMinKeyLength);
        } else {
            key_ = std::move(decoded);
            key_is_persistent_ = true;
            LOG_INFO("stateless reset key loaded from %s", kKeyEnvVar);
            return;
        }
    }

    // Fallback: random per-process key. Tokens stay unguessable, but they change
    // on restart, so peers of connections from a previous process will not be
    // reset and must wait for idle timeout. Set the env var in production.
    key_.resize(kMinKeyLength);
    if (RAND_bytes(reinterpret_cast<uint8_t*>(&key_[0]), static_cast<int>(key_.size())) != 1) {
        LOG_ERROR("CSPRNG failed while generating a stateless reset key");
        key_.clear();
        return;
    }
    LOG_WARN(
        "%s not set; using a per-process stateless reset key. Connections from a previous process "
        "will not be reset after a restart.",
        kKeyEnvVar);
}

StatelessResetTokenGenerator& StatelessResetTokenGenerator::Instance() {
    // Function-local static: thread-safe initialization since C++11.
    static StatelessResetTokenGenerator instance;
    return instance;
}

bool StatelessResetTokenGenerator::Generate(const uint8_t* cid, uint8_t cid_len, uint8_t* out) const {
    if (out == nullptr || key_.empty()) {
        return false;
    }

    uint8_t digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    // A zero-length CID is legal, and HMAC over empty data is well defined; pass a
    // valid non-null pointer for that case to stay clear of UB.
    static const uint8_t kEmpty = 0;
    const uint8_t* data = (cid != nullptr && cid_len > 0) ? cid : &kEmpty;
    const size_t data_len = (cid != nullptr && cid_len > 0) ? cid_len : 0;

    if (HMAC(EVP_sha256(), key_.data(), static_cast<int>(key_.size()), data, data_len, digest, &digest_len) ==
            nullptr ||
        digest_len < kTokenLength) {
        LOG_ERROR("HMAC failed while deriving a stateless reset token");
        return false;
    }

    memcpy(out, digest, kTokenLength);
    return true;
}

bool StatelessResetTokenGenerator::Verify(const uint8_t* cid, uint8_t cid_len, const uint8_t* token) const {
    if (token == nullptr) {
        return false;
    }

    uint8_t expected[kTokenLength];
    if (!Generate(cid, cid_len, expected)) {
        return false;
    }

    // Constant time: see the note on Verify() in the header.
    return CRYPTO_memcmp(expected, token, kTokenLength) == 0;
}

}  // namespace quic
}  // namespace quicx
