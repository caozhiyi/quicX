#ifndef QUIC_CONNECTION_CONNECTION_CRYPTO
#define QUIC_CONNECTION_CONNECTION_CRYPTO

#include "quic/common/version.h"
#include "quic/connection/transport_param.h"
#include "quic/crypto/if_cryptographer.h"
#include "quic/crypto/tls/tls_connection.h"
#include "quic/crypto/tls/type.h"
#include "quic/stream/crypto_stream.h"

namespace quicx {
namespace common {
class QlogTrace;
}
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

    std::shared_ptr<ICryptographer> GetCryptographer(uint8_t level) const { return cryptographers_[level]; }

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

    // Reset Initial cryptographer (used for Retry or version negotiation).
    // Also drops any pre-upgrade Initial keys: a reset means the Initial
    // keying is being redone from scratch, so a leftover older generation
    // would only be a stale key sitting in memory.
    void Reset() {
        cryptographers_[kInitial] = nullptr;
        prev_initial_cryptographer_ = nullptr;
        prev_initial_version_ = 0;
    }

    // RFC 9368 Compatible Version Negotiation: Re-derive Initial secrets using a new
    // QUIC version's salt, with the same DCID. Used after the endpoint decides to
    // switch from the client's chosen_version (e.g. v1) to a compatible preferred
    // version (e.g. v2). The existing Initial cryptographer is discarded and a new
    // one is installed with the new version's labels and salt.
    // @param new_version  The QUIC version to switch to.
    // @param dcid         The DCID used to derive the initial_secret.
    // @param dcid_len     Length of |dcid|.
    // @param is_server    True on server side, false on client side.
    // @return true on success.
    bool RekeyInitialForVersion(uint32_t new_version, const uint8_t* dcid, uint32_t dcid_len, bool is_server);

    // Selects the Initial keys that can read a packet carrying |pkt_version|.
    //
    // After a Compatible VN upgrade a connection legitimately holds Initial
    // keys for TWO versions at once: the negotiated one (used for everything we
    // send and receive from now on) and the pre-upgrade one, retained for
    // decryption only because the peer keeps retransmitting its first flight
    // under the original version until our upgrade reaches it. Nothing but the
    // version field of the long header distinguishes the two, so inbound
    // Initial handling must resolve keys through here instead of reaching
    // straight for GetCryptographer(kInitial).
    //
    // @param pkt_version  Version from the inbound long header. 0 means the
    //                     caller has no version to match on, in which case the
    //                     current keys are returned.
    // @return The matching Initial cryptographer, or nullptr when we hold no
    //         Initial key for that version — RFC 9369 §4.1 requires the caller
    //         to drop such a packet.
    std::shared_ptr<ICryptographer> GetInitialCryptographerForVersion(uint32_t pkt_version) const;

    // RFC 9369 §4.1: "The server MUST NOT discard its original version Initial
    // keys until it successfully processes a packet with the negotiated
    // version." Releases the pre-upgrade Initial keys retained by
    // RekeyInitialForVersion; no-op when no compatible upgrade happened.
    void DiscardPreviousInitialKeys();

    // RFC 9368: Returns the DCID that was used to derive the currently installed
    // Initial secret. After a Compatible VN rekey this is updated accordingly.
    // Returns empty string if no Initial secret was ever installed.
    const std::string& GetInitialSecretDcid() const { return initial_secret_dcid_; }

    typedef std::function<void(TransportParam&)> RemoteTransportParamCB;
    void SetRemoteTransportParamCB(RemoteTransportParamCB cb) { transport_param_cb_ = cb; }

    // Callback invoked when 0-RTT write key is installed (early data ready)
    typedef std::function<void()> EarlyDataReadyCB;
    void SetEarlyDataReadyCB(EarlyDataReadyCB cb) { early_data_ready_cb_ = cb; }

    // RFC 9001 §4.8: a fatal TLS alert during the handshake must be surfaced to the
    // peer as a QUIC CONNECTION_CLOSE carrying a CRYPTO_ERROR (0x0100 + alert). This
    // callback lets the owning connection drive that close so a failed handshake is
    // not silently dropped.
    typedef std::function<void(uint64_t error, const std::string& reason)> HandshakeErrorCB;
    void SetHandshakeErrorCB(HandshakeErrorCB cb) { handshake_error_cb_ = cb; }

    // Role of the owning connection. Needed when validating the peer's transport
    // parameters, because RFC 9000 §18.2 reserves several parameters for servers only.
    void SetIsServer(bool is_server) { is_server_ = is_server; }

    // RFC 9001 Section 6: Key Update
    // Trigger a key update on the Application level cryptographer
    // Returns true if key update was successful
    bool TriggerKeyUpdate();

    // RFC 9001 Section 6: Passive Key Update (read keys only)
    // Called when receiving a packet with a different Key Phase bit
    // Updates read keys to the next generation, and also updates write keys
    // Returns true if key update was successful
    bool TriggerReadKeyUpdate();

    // Key Phase tracking for passive Key Update detection
    uint8_t GetCurrentKeyPhase() const { return current_key_phase_; }
    void FlipKeyPhase() { current_key_phase_ ^= 1; }

    // Check if Application level cryptographer is ready for key updates
    bool CanKeyUpdate() const { return cryptographers_[kApplication] != nullptr; }

    // Version management
    void SetVersion(uint32_t version) { quic_version_ = version; }
    uint32_t GetVersion() const { return quic_version_; }

    // Qlog trace for security events
    void SetQlogTrace(std::shared_ptr<common::QlogTrace> trace) { qlog_trace_ = trace; }

private:
    bool transport_param_done_;
    bool is_server_{false};
    RemoteTransportParamCB transport_param_cb_;
    EarlyDataReadyCB early_data_ready_cb_;
    HandshakeErrorCB handshake_error_cb_;

    EncryptionLevel cur_encryption_level_;
    std::shared_ptr<CryptoStream> crypto_stream_;
    std::shared_ptr<ICryptographer> cryptographers_[kNumEncryptionLevels];

    // Initial keys of the version this connection spoke before a Compatible VN
    // upgrade, kept alive so the peer's in-flight Initial packets (still
    // encrypted under the original salt/labels) remain decryptable. Null unless
    // an upgrade actually happened; released by DiscardPreviousInitialKeys().
    // |prev_initial_version_| is the version those keys belong to and is only
    // meaningful while the cryptographer is non-null.
    std::shared_ptr<ICryptographer> prev_initial_cryptographer_;
    uint32_t prev_initial_version_ = 0;

    // QUIC version for this connection (default to v2 as preferred)
    uint32_t quic_version_ = kQuicVersion2;

    // RFC 9001 §6: Current key phase (0 or 1), flips on each Key Update
    uint8_t current_key_phase_ = 0;

    // RFC 9368: DCID used to derive the currently installed Initial secret.
    // Persisted so that Compatible VN rekey can reuse the same DCID even when
    // the initiating side (BaseConnection::OnInitialPacket for the client)
    // does not have direct access to ClientConnection::original_dcid_.
    std::string initial_secret_dcid_;

    // Qlog trace for security events
    std::shared_ptr<common::QlogTrace> qlog_trace_;
};

}  // namespace quic
}  // namespace quicx

#endif