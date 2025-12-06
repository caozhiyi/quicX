#include <cstring>

#include "common/buffer/buffer_read_view.h"
#include "common/log/log.h"

#include "quic/connection/connection_crypto.h"
#include "quic/crypto/hkdf.h"
#include "quic/crypto/if_cryptographer.h"
#include "quic/crypto/type.h"
#include "quic/stream/crypto_stream.h"

namespace quicx {
namespace quic {

ConnectionCrypto::ConnectionCrypto():
    cur_encryption_level_(kInitial),
    transport_param_done_(false) {
    memset(cryptographers_, 0, sizeof(std::shared_ptr<ICryptographer>) * kNumEncryptionLevels);
}

ConnectionCrypto::~ConnectionCrypto() {}

void ConnectionCrypto::SetReadSecret(
    SSL* ssl, EncryptionLevel level, const SSL_CIPHER* cipher, const uint8_t* secret, size_t secret_len) {
    std::shared_ptr<ICryptographer> cryptographer = cryptographers_[level];
    if (cryptographer == nullptr) {
        cryptographer = MakeCryptographer(cipher);
        cryptographers_[level] = cryptographer;
    }
    cur_encryption_level_ = level;
    cryptographer->InstallSecret(secret, (uint32_t)secret_len, false);
}

void ConnectionCrypto::SetWriteSecret(
    SSL* ssl, EncryptionLevel level, const SSL_CIPHER* cipher, const uint8_t* secret, size_t secret_len) {
    std::shared_ptr<ICryptographer> cryptographer = cryptographers_[level];
    if (cryptographer == nullptr) {
        cryptographer = MakeCryptographer(cipher);
        cryptographers_[level] = cryptographer;
    }
    cur_encryption_level_ = level;
    cryptographer->InstallSecret(secret, (uint32_t)secret_len, true);
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
    if (cryptographers_[kInitial]) {
        return false;
    }

    // make initial cryptographer
    std::shared_ptr<ICryptographer> cryptographer = MakeCryptographer(kCipherIdAes128GcmSha256);
    cryptographer->InstallInitSecret(secret, len, kInitialSalt.data(), kInitialSalt.size(), is_server);
    cryptographers_[kInitial] = cryptographer;
    return true;
}

bool ConnectionCrypto::InstallInitSecretForRetry(
    const uint8_t* write_cid, uint32_t write_len, const uint8_t* read_cid, uint32_t read_len) {
    // RFC 9000: After Retry, client needs asymmetric Initial Secrets:
    // - Write secret derived from Retry's Source CID (new DCID for client's packets)
    // - Read secret derived from client's own Source CID (server uses this as DCID)

    if (cryptographers_[kInitial]) {
        return false;
    }

    // Strategy: Install with local CID first (sets correct read), then manually update write
    // Step 1: Create cryptographer and install with local CID using is_server=false
    // This gives us: read="server in", write="client in"
    // But we're using local_cid, so read is correct (server will encrypt with our local cid)
    std::shared_ptr<ICryptographer> cryptographer = MakeCryptographer(kCipherIdAes128GcmSha256);
    cryptographer->InstallInitSecret(read_cid, read_len, kInitialSalt.data(), kInitialSalt.size(), false);

    // Step 2: Now manually derive and install write secret using Retry Source CID
    // We need to derive: init_secret = HKDF-Extract(retry_cid, salt)
    //                    write_secret = HKDF-Expand(init_secret, "client in", hash_len)
    // Then call InstallSecret(write_secret, true) to update only write

    // Import HKDF from crypto module
    const EVP_MD* digest = EVP_sha256();
    uint8_t init_secret[32] = {0};

    // HKDF-Extract
    if (!Hkdf::HkdfExtract(init_secret, 32, write_cid, write_len, kInitialSalt.data(), kInitialSalt.size(), digest)) {
        return false;
    }

    // HKDF-Expand with "client in" label
    uint8_t write_secret[32] = {0};
    if (!Hkdf::HkdfExpand(write_secret, 32, init_secret, 32, kTlsLabelClient.data(), kTlsLabelClient.size(), digest)) {
        return false;
    }

    // Install write secret (is_write=true)
    if (cryptographer->InstallSecret(write_secret, 32, true) != ICryptographer::Result::kOk) {
        return false;
    }

    cryptographers_[kInitial] = cryptographer;
    return true;
}

}  // namespace quic
}  // namespace quicx
