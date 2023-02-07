#include <cstring>
#include "common/log/log.h"
#include "quic/connection/connection_interface.h"
#include "quic/crypto/aes_128_gcm_cryptographer.h"
#include "quic/crypto/aes_256_gcm_cryptographer.h"
#include "quic/crypto/chacha20_poly1305_cryptographer.h"

namespace quicx {

IConnection::IConnection() {
    memset(_cryptographers, 0, sizeof(std::shared_ptr<ICryptographer>) * __crypto_level_count);
}

IConnection::~IConnection() {

}

void IConnection::SetReadSecret(SSL* ssl, ssl_encryption_level_t level, const SSL_CIPHER *cipher,
    const uint8_t *secret, size_t secret_len) {
    std::shared_ptr<ICryptographer> cryptographer = _cryptographers[level];
    if (cryptographer == nullptr) {
        cryptographer = MakeCryptographer(cipher);
        _cryptographers[level] = cryptographer;
    }
    cryptographer->InstallSecret(secret, (uint32_t)secret_len, false);
}

void IConnection::SetWriteSecret(SSL* ssl, ssl_encryption_level_t level, const SSL_CIPHER *cipher,
    const uint8_t *secret, size_t secret_len) {
    std::shared_ptr<ICryptographer> cryptographer = _cryptographers[level];
    if (cryptographer == nullptr) {
        cryptographer = MakeCryptographer(cipher);
        _cryptographers[level] = cryptographer;
    }
    cryptographer->InstallSecret(secret, (uint32_t)secret_len, true);
}

void IConnection::WriteMessage(ssl_encryption_level_t level, const uint8_t *data,
    size_t len) {
    
}

void IConnection::FlushFlight() {

}

void IConnection::SendAlert(ssl_encryption_level_t level, uint8_t alert) {

}

void IConnection::HandlePacket(std::vector<std::shared_ptr<IPacket>>& packets) {
    for (size_t i = 0; i < packets.size(); i++) {
        /*switch (packets[i]->GetPacketType())
        {
        case PT_INITIAL:
            if (!HandleInitial(std::dynamic_pointer_cast<InitPacket>(packets[i]))) {
                LOG_ERROR("init packet handle failed.");
            }
            break;
        case PT_0RTT:
            if (!Handle0rtt(std::dynamic_pointer_cast<Rtt0Packet>(packets[i]))) {
                LOG_ERROR("0 rtt packet handle failed.");
            }
            break;
        case PT_HANDSHAKE:
            if (!HandleHandshake(std::dynamic_pointer_cast<HandShakePacket>(packets[i]))) {
                LOG_ERROR("handshakee packet handle failed.");
            }
            break;
        case PT_RETRY:
            if (!HandleRetry(std::dynamic_pointer_cast<RetryPacket>(packets[i]))) {
                LOG_ERROR("retry packet handle failed.");
            }
            break;
        case PT_1RTT:
            if (!Handle1rtt(std::dynamic_pointer_cast<Rtt1Packet>(packets[i]))) {
                LOG_ERROR("init packet handle failed.");
            }
            break;
        default:
            LOG_ERROR("unknow packet type. type:%d", packets[i]->GetPacketType());
            break;
        }*/
    }
}

}