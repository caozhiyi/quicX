#include <cstring>
#include "common/log/log.h"
#include "common/buffer/buffer_read_view.h"
#include "quic/connection/connection_crypto.h"

namespace quicx {
namespace quic {

ConnectionCrypto::ConnectionCrypto():
    _cur_encryption_level(EL_INITIAL),
    _transport_param_done(false) {
    memset(_cryptographers, 0, sizeof(std::shared_ptr<ICryptographer>) * NUM_ENCRYPTION_LEVELS);
}

ConnectionCrypto::~ConnectionCrypto() {

}

void ConnectionCrypto::SetReadSecret(SSL* ssl, EncryptionLevel level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len) {
    std::shared_ptr<ICryptographer> cryptographer = _cryptographers[level];
    if (cryptographer == nullptr) {
        cryptographer = MakeCryptographer(cipher);
        _cryptographers[level] = cryptographer;
    }
    _cur_encryption_level = level;
    cryptographer->InstallSecret(secret, (uint32_t)secret_len, false);
}

void ConnectionCrypto::SetWriteSecret(SSL* ssl, EncryptionLevel level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len) {
    std::shared_ptr<ICryptographer> cryptographer = _cryptographers[level];
    if (cryptographer == nullptr) {
        cryptographer = MakeCryptographer(cipher);
        _cryptographers[level] = cryptographer;
    }
    _cur_encryption_level = level;
    cryptographer->InstallSecret(secret, (uint32_t)secret_len, true);
}

void ConnectionCrypto::WriteMessage(EncryptionLevel level, const uint8_t *data,
        size_t len) {
    _crypto_stream->Send((uint8_t*)data, len, level);
}

void ConnectionCrypto::FlushFlight() {
    // TODO close connectoin whit error
}

void ConnectionCrypto::SendAlert(EncryptionLevel level, uint8_t alert) {

}

void ConnectionCrypto::OnTransportParams(EncryptionLevel level, const uint8_t* tp, size_t tp_len) {
    if (_transport_param_done) {
        return;
    }
    _transport_param_done = true;

    TransportParam remote_tp;
    std::shared_ptr<common::IBufferRead> buffer = std::make_shared<common::BufferReadView>((uint8_t*)tp, tp_len);
    if (!remote_tp.Decode(buffer)) {
        common::LOG_ERROR("decode remote transport failed.");
        return;
    }
    if (_transport_param_cb) {
        _transport_param_cb(remote_tp);
    }
}

EncryptionLevel ConnectionCrypto::GetCurEncryptionLevel() {
    uint8_t level = _crypto_stream->GetWaitSendEncryptionLevel();
    return (EncryptionLevel)std::min<uint8_t>(level, _cur_encryption_level);
}

void ConnectionCrypto::SetCryptoStream(std::shared_ptr<CryptoStream> crypto_stream) {
    _crypto_stream = crypto_stream;
}

void ConnectionCrypto::OnCryptoFrame(std::shared_ptr<IFrame> frame) {
    _crypto_stream->OnFrame(frame);
}

bool ConnectionCrypto::InstallInitSecret(uint8_t* secret, uint32_t len, bool is_server) {
    if (_cryptographers[EL_INITIAL]) {
        return false;
    }
    
    // make initial cryptographer
    std::shared_ptr<ICryptographer> cryptographer = MakeCryptographer(CI_TLS1_CK_AES_128_GCM_SHA256);
    cryptographer->InstallInitSecret(secret, len, __initial_slat, sizeof(__initial_slat), is_server);
    _cryptographers[EL_INITIAL] = cryptographer;
    return true;
}

}
}
