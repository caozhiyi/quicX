#ifndef QUIC_CONNECTION_CONNECTION_CRYPTO
#define QUIC_CONNECTION_CONNECTION_CRYPTO

#include "quic/crypto/tls/type.h"
#include "quic/stream/crypto_stream.h"
#include "quic/crypto/if_cryptographer.h"
#include "quic/crypto/tls/tls_conneciton.h"
#include "quic/connection/transport_param.h"

namespace quicx {
namespace quic {

class ConnectionCrypto:
    public TlsHandlerInterface {
public:
    ConnectionCrypto();
    virtual ~ConnectionCrypto();

    // Encryption correlation function
    virtual void SetReadSecret(SSL* ssl, EncryptionLevel level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len);

    virtual void SetWriteSecret(SSL* ssl, EncryptionLevel level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len);

    virtual void WriteMessage(EncryptionLevel level, const uint8_t *data,
        size_t len);

    virtual void FlushFlight();
    virtual void SendAlert(EncryptionLevel level, uint8_t alert);

    virtual void OnTransportParams(EncryptionLevel level, const uint8_t* tp, size_t tp_len);

    EncryptionLevel GetCurEncryptionLevel();

    std::shared_ptr<ICryptographer> GetCryptographer(uint8_t level) { return cryptographers_[level]; }

    void SetCryptoStream(std::shared_ptr<CryptoStream> crypto_stream);

    void OnCryptoFrame(std::shared_ptr<IFrame> frame);

    bool InitIsReady() { return cryptographers_[EL_INITIAL] != nullptr; }
    bool InstallInitSecret(uint8_t* secret, uint32_t len, bool is_server);

    typedef std::function<void(TransportParam&)> RemoteTransportParamCB;
    void SetRemoteTransportParamCB(RemoteTransportParamCB cb) { transport_param_cb_ = cb; }

private:
    bool transport_param_done_;
    RemoteTransportParamCB transport_param_cb_;

    EncryptionLevel cur_encryption_level_;
    std::shared_ptr<CryptoStream> crypto_stream_;
    std::shared_ptr<ICryptographer> cryptographers_[NUM_ENCRYPTION_LEVELS];
};

}
}

#endif