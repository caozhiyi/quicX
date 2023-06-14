#ifndef QUIC_CONNECTION_CONNECTION_CRYPTO
#define QUIC_CONNECTION_CONNECTION_CRYPTO

#include "quic/crypto/tls/type.h"
#include "quic/stream/crypto_stream.h"
#include "quic/crypto/tls/tls_conneciton.h"
#include "quic/connection/transport_param.h"
#include "quic/crypto/cryptographer_interface.h"

namespace quicx {

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

    EncryptionLevel GetCurEncryptionLevel() { return _cur_encryption_level; }

    std::shared_ptr<ICryptographer> GetCryptographer(uint8_t level) { return _cryptographers[level]; }

    void SetCryptoStream(std::shared_ptr<CryptoStream> crypto_stream);

    void OnCryptoFrame(std::shared_ptr<IFrame> frame);

    bool InitIsReady() { return _cryptographers[EL_INITIAL] != nullptr; }
    bool InstallInitSecret(uint8_t* secret, uint32_t len, bool is_server);

    typedef std::function<void(TransportParam&)> RemoteTransportParamCB;
    void SetRemoteTransportParamCB(RemoteTransportParamCB cb) { _transport_param_cb = cb; }

private:
    bool _transport_param_done;
    RemoteTransportParamCB _transport_param_cb;

    EncryptionLevel _cur_encryption_level;
    std::shared_ptr<CryptoStream> _crypto_stream;
    std::shared_ptr<ICryptographer> _cryptographers[NUM_ENCRYPTION_LEVELS];
};

}

#endif