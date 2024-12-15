#include <cstring>
#include "common/log/log.h"
#include "common/buffer/buffer_read_view.h"
#include "quic/connection/connection_crypto.h"

namespace quicx {
namespace quic {

ConnectionCrypto::ConnectionCrypto():
    cur_encryption_level_(EL_INITIAL),
    transport_param_done_(false) {
    memset(cryptographers_, 0, sizeof(std::shared_ptr<ICryptographer>) * NUM_ENCRYPTION_LEVELS);
}

ConnectionCrypto::~ConnectionCrypto() {

}

void ConnectionCrypto::SetReadSecret(SSL* ssl, EncryptionLevel level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len) {
    std::shared_ptr<ICryptographer> cryptographer = cryptographers_[level];
    if (cryptographer == nullptr) {
        cryptographer = MakeCryptographer(cipher);
        cryptographers_[level] = cryptographer;
    }
    cur_encryption_level_ = level;
    cryptographer->InstallSecret(secret, (uint32_t)secret_len, false);
}

void ConnectionCrypto::SetWriteSecret(SSL* ssl, EncryptionLevel level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len) {
    std::shared_ptr<ICryptographer> cryptographer = cryptographers_[level];
    if (cryptographer == nullptr) {
        cryptographer = MakeCryptographer(cipher);
        cryptographers_[level] = cryptographer;
    }
    cur_encryption_level_ = level;
    cryptographer->InstallSecret(secret, (uint32_t)secret_len, true);
}

void ConnectionCrypto::WriteMessage(EncryptionLevel level, const uint8_t *data,
        size_t len) {
    crypto_stream_->Send((uint8_t*)data, len, level);
}

void ConnectionCrypto::FlushFlight() {
    // TODO close connectoin whit error
}

void ConnectionCrypto::SendAlert(EncryptionLevel level, uint8_t alert) {

}

void ConnectionCrypto::OnTransportParams(EncryptionLevel level, const uint8_t* tp, size_t tp_len) {
    if (transport_param_done_) {
        return;
    }
    transport_param_done_ = true;

    TransportParam remote_tp;
    std::shared_ptr<common::IBufferRead> buffer = std::make_shared<common::BufferReadView>((uint8_t*)tp, tp_len);
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

bool ConnectionCrypto::InstallInitSecret(uint8_t* secret, uint32_t len, bool is_server) {
    if (cryptographers_[EL_INITIAL]) {
        return false;
    }
    
    // make initial cryptographer
    std::shared_ptr<ICryptographer> cryptographer = MakeCryptographer(CI_TLS1_CK_AES_128_GCM_SHA256);
    cryptographer->InstallInitSecret(secret, len, __initial_slat, sizeof(__initial_slat), is_server);
    cryptographers_[EL_INITIAL] = cryptographer;
    return true;
}

}
}
