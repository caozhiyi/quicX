#ifndef QUIC_CONNECTION_CONNECTION_CRYPTO
#define QUIC_CONNECTION_CONNECTION_CRYPTO

#include "quic/common/version.h"
#include "quic/connection/transport_param.h"
#include "quic/crypto/if_cryptographer.h"
#include "quic/crypto/tls/tls_connection.h"
#include "quic/crypto/tls/type.h"
#include "quic/stream/crypto_stream.h"

namespace quicx {
namespace quic {

class ConnectionCrypto: public TlsHandlerInterface {
public:
    ConnectionCrypto();
    virtual ~ConnectionCrypto();

    // Encryption correlation function
    virtual void SetReadSecret(
        SSL* ssl, EncryptionLevel level, const SSL_CIPHER* cipher, const uint8_t* secret, size_t secret_len);

    virtual void SetWriteSecret(
        SSL* ssl, EncryptionLevel level, const SSL_CIPHER* cipher, const uint8_t* secret, size_t secret_len);

    virtual void WriteMessage(EncryptionLevel level, const uint8_t* data, size_t len);

    virtual void FlushFlight();
    virtual void SendAlert(EncryptionLevel level, uint8_t alert);

    virtual void OnTransportParams(EncryptionLevel level, const uint8_t* tp, size_t tp_len);

    EncryptionLevel GetCurEncryptionLevel();

    std::shared_ptr<ICryptographer> GetCryptographer(uint8_t level) { return cryptographers_[level]; }

    void SetCryptoStream(std::shared_ptr<CryptoStream> crypto_stream);
    std::shared_ptr<CryptoStream> GetCryptoStream() { return crypto_stream_; }

    void OnCryptoFrame(std::shared_ptr<IFrame> frame);

    bool InitIsReady() { return cryptographers_[kInitial] != nullptr; }
    
    // Legacy: Install Initial secret with default (v1) salt
    bool InstallInitSecret(const uint8_t* secret, uint32_t len, bool is_server);
    
    // Version-aware: Install Initial secret with version-specific salt (RFC 9369)
    bool InstallInitSecretWithVersion(const uint8_t* secret, uint32_t len, uint32_t version, bool is_server);

    // RFC 9000: Install asymmetric Initial Secrets for Retry
    // write_cid: CID for encrypting outbound packets (Retry's Source CID)
    // read_cid: CID for decrypting inbound packets (client's local Source CID)
    bool InstallInitSecretForRetry(
        const uint8_t* write_cid, uint32_t write_len, const uint8_t* read_cid, uint32_t read_len);
    
    // Version-aware Retry secret installation
    bool InstallInitSecretForRetryWithVersion(
        const uint8_t* write_cid, uint32_t write_len, const uint8_t* read_cid, uint32_t read_len, uint32_t version);

    // Reset Initial cryptographer (used for Retry or version negotiation)
    void Reset() { cryptographers_[kInitial] = nullptr; }

    typedef std::function<void(TransportParam&)> RemoteTransportParamCB;
    void SetRemoteTransportParamCB(RemoteTransportParamCB cb) { transport_param_cb_ = cb; }

    // Callback invoked when 0-RTT write key is installed (early data ready)
    typedef std::function<void()> EarlyDataReadyCB;
    void SetEarlyDataReadyCB(EarlyDataReadyCB cb) { early_data_ready_cb_ = cb; }

    // RFC 9001 Section 6: Key Update
    // Trigger a key update on the Application level cryptographer
    // Returns true if key update was successful
    bool TriggerKeyUpdate();

    // Check if Application level cryptographer is ready for key updates
    bool CanKeyUpdate() const { return cryptographers_[kApplication] != nullptr; }
    
    // Version management
    void SetVersion(uint32_t version) { quic_version_ = version; }
    uint32_t GetVersion() const { return quic_version_; }

private:
    bool transport_param_done_;
    RemoteTransportParamCB transport_param_cb_;
    EarlyDataReadyCB early_data_ready_cb_;

    EncryptionLevel cur_encryption_level_;
    std::shared_ptr<CryptoStream> crypto_stream_;
    std::shared_ptr<ICryptographer> cryptographers_[kNumEncryptionLevels];
    
    // QUIC version for this connection (default to v2 as preferred)
    uint32_t quic_version_ = kQuicVersion2;
};

}  // namespace quic
}  // namespace quicx

#endif