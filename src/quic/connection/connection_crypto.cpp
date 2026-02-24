#include <cstring>

#include "common/buffer/buffer_read_view.h"
#include "common/log/log.h"

#include "quic/common/version.h"
#include "quic/connection/connection_crypto.h"
#include "quic/crypto/hkdf.h"
#include "quic/crypto/if_cryptographer.h"
#include "quic/crypto/type.h"
#include "quic/stream/crypto_stream.h"

namespace quicx {
namespace quic {

ConnectionCrypto::ConnectionCrypto():
    cur_encryption_level_(kInitial),
    transport_param_done_(false),
    quic_version_(GetDefaultVersion()) {
    memset(cryptographers_, 0, sizeof(std::shared_ptr<ICryptographer>) * kNumEncryptionLevels);
}

ConnectionCrypto::~ConnectionCrypto() {}

void ConnectionCrypto::SetReadSecret(
    SSL* ssl, EncryptionLevel level, const SSL_CIPHER* cipher, const uint8_t* secret, size_t secret_len) {
    std::shared_ptr<ICryptographer> cryptographer = cryptographers_[level];
    if (cryptographer == nullptr) {
        cryptographer = MakeCryptographer(cipher);
        cryptographer->SetVersion(quic_version_);
        cryptographers_[level] = cryptographer;
    }
    cur_encryption_level_ = level;
    cryptographer->InstallSecretWithVersion(secret, (uint32_t)secret_len, false, quic_version_);
}

void ConnectionCrypto::SetWriteSecret(
    SSL* ssl, EncryptionLevel level, const SSL_CIPHER* cipher, const uint8_t* secret, size_t secret_len) {
    std::shared_ptr<ICryptographer> cryptographer = cryptographers_[level];
    if (cryptographer == nullptr) {
        cryptographer = MakeCryptographer(cipher);
        cryptographer->SetVersion(quic_version_);
        cryptographers_[level] = cryptographer;
    }
    cur_encryption_level_ = level;
    cryptographer->InstallSecretWithVersion(secret, (uint32_t)secret_len, true, quic_version_);

    // When 0-RTT write key is installed, notify the connection that early data can be sent
    if (level == kEarlyData && early_data_ready_cb_) {
        common::LOG_INFO("0-RTT write key installed, triggering early data ready callback");
        early_data_ready_cb_();
    }
}

void ConnectionCrypto::WriteMessage(EncryptionLevel level, const uint8_t* data, size_t len) {
    crypto_stream_->Send((uint8_t*)data, len, level);
}

void ConnectionCrypto::FlushFlight() {
    // TODO close connectoin whit error
}

void ConnectionCrypto::SendAlert(EncryptionLevel level, uint8_t alert) {}

void ConnectionCrypto::OnTransportParams(EncryptionLevel level, const uint8_t* tp, size_t tp_len) {
    if (transport_param_done_) {
        return;
    }
    transport_param_done_ = true;

    TransportParam remote_tp;
    common::BufferReadView buffer((uint8_t*)tp, tp_len);
    if (!remote_tp.Decode(buffer)) {
        common::LOG_ERROR("decode remote transport failed.");
        return;
    }
    if (transport_param_cb_) {
        transport_param_cb_(remote_tp);
    }
}

EncryptionLevel ConnectionCrypto::GetCurEncryptionLevel() {
    uint8_t level = crypto_stream_->GetWaitSendEncryptionLevel();
    return (EncryptionLevel)std::min<uint8_t>(level, cur_encryption_level_);
}

void ConnectionCrypto::SetCryptoStream(std::shared_ptr<CryptoStream> crypto_stream) {
    crypto_stream_ = crypto_stream;
}

void ConnectionCrypto::OnCryptoFrame(std::shared_ptr<IFrame> frame) {
    crypto_stream_->OnFrame(frame);
}

bool ConnectionCrypto::InstallInitSecret(const uint8_t* secret, uint32_t len, bool is_server) {
    // Use version-aware method with current version
    return InstallInitSecretWithVersion(secret, len, quic_version_, is_server);
}

bool ConnectionCrypto::InstallInitSecretWithVersion(const uint8_t* secret, uint32_t len, uint32_t version, bool is_server) {
    if (cryptographers_[kInitial]) {
        return false;
    }
    
    // Update stored version
    quic_version_ = version;

    // make initial cryptographer
    std::shared_ptr<ICryptographer> cryptographer = MakeCryptographer(kCipherIdAes128GcmSha256);
    cryptographer->SetVersion(version);
    
    // Use version-aware initial secret installation
    cryptographer->InstallInitSecretWithVersion(secret, len, version, is_server);
    cryptographers_[kInitial] = cryptographer;
    
    common::LOG_INFO("Installed Initial secret with version %s (0x%08x)", VersionToString(version), version);
    return true;
}

bool ConnectionCrypto::InstallInitSecretForRetry(
    const uint8_t* write_cid, uint32_t write_len, const uint8_t* read_cid, uint32_t read_len) {
    return InstallInitSecretForRetryWithVersion(write_cid, write_len, read_cid, read_len, quic_version_);
}

bool ConnectionCrypto::InstallInitSecretForRetryWithVersion(
    const uint8_t* write_cid, uint32_t write_len, const uint8_t* read_cid, uint32_t read_len, uint32_t version) {
    // RFC 9000: After Retry, client needs asymmetric Initial Secrets:
    // - Write secret derived from Retry's Source CID (new DCID for client's packets)
    // - Read secret derived from client's own Source CID (server uses this as DCID)

    if (cryptographers_[kInitial]) {
        return false;
    }
    
    // Update stored version
    quic_version_ = version;
    
    // Get version-specific salt
    const uint8_t* salt = GetInitialSalt(version);
    size_t salt_len = GetInitialSaltLength(version);

    // Strategy: Install with local CID first (sets correct read), then manually update write
    // Step 1: Create cryptographer and install with local CID using is_server=false
    // This gives us: read="server in", write="client in"
    // But we're using local_cid, so read is correct (server will encrypt with our local cid)
    std::shared_ptr<ICryptographer> cryptographer = MakeCryptographer(kCipherIdAes128GcmSha256);
    cryptographer->SetVersion(version);
    cryptographer->InstallInitSecret(read_cid, read_len, salt, salt_len, false);

    // Step 2: Now manually derive and install write secret using Retry Source CID
    // We need to derive: init_secret = HKDF-Extract(retry_cid, salt)
    //                    write_secret = HKDF-Expand(init_secret, "client in", hash_len)
    // Then call InstallSecret(write_secret, true) to update only write

    // Import HKDF from crypto module
    const EVP_MD* digest = EVP_sha256();
    uint8_t init_secret[32] = {0};

    // HKDF-Extract
    if (!Hkdf::HkdfExtract(init_secret, 32, write_cid, write_len, salt, salt_len, digest)) {
        return false;
    }

    // HKDF-Expand with "client in" label
    uint8_t write_secret[32] = {0};
    if (!Hkdf::HkdfExpand(write_secret, 32, init_secret, 32, kTlsLabelClient.data(), kTlsLabelClient.size(), digest)) {
        return false;
    }

    // Install write secret (is_write=true) with version-aware labels
    if (cryptographer->InstallSecretWithVersion(write_secret, 32, true, version) != ICryptographer::Result::kOk) {
        return false;
    }

    cryptographers_[kInitial] = cryptographer;
    common::LOG_INFO("Installed Initial secret for Retry with version %s (0x%08x)", VersionToString(version), version);
    return true;
}

bool ConnectionCrypto::TriggerKeyUpdate() {
    // RFC 9001 Section 6: Key Update
    // Key updates can only be performed on Application level keys
    auto cryptographer = cryptographers_[kApplication];
    if (!cryptographer) {
        common::LOG_ERROR("Cannot trigger key update: Application level cryptographer not ready");
        return false;
    }

    // Trigger key update for both read and write directions using version-aware method
    // First update write keys (our outgoing traffic)
    auto result = cryptographer->KeyUpdateWithVersion(nullptr, 0, true, quic_version_);
    if (result != ICryptographer::Result::kOk) {
        common::LOG_ERROR("Key update failed for write keys: %d", static_cast<int>(result));
        return false;
    }

    // Then update read keys (incoming traffic)
    // Note: In practice, we might want to delay read key update until we receive
    // a packet with the new Key Phase bit, but for simplicity we update both here
    result = cryptographer->KeyUpdateWithVersion(nullptr, 0, false, quic_version_);
    if (result != ICryptographer::Result::kOk) {
        common::LOG_ERROR("Key update failed for read keys: %d", static_cast<int>(result));
        return false;
    }

    common::LOG_INFO("Key update completed successfully (version: %s)", VersionToString(quic_version_));
    return true;
}

}  // namespace quic
}  // namespace quicx
