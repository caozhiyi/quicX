#include <gtest/gtest.h>
#include <openssl/bio.h>
#include <openssl/mem.h>
#include <openssl/pem.h>
#include "quic/crypto/type.h"
#include "quic/crypto/tls/type.h"
#include "common/util/c_smart_ptr.h"
#include "quic/crypto/tls/tls_client_ctx.h"
#include "quic/crypto/tls/tls_server_ctx.h"
#include "quic/crypto/tls/tls_client_conneciton.h"
#include "quic/crypto/tls/tls_server_conneciton.h"

static uint8_t kALPNProtos[] = {0x03, 'f', 'o', 'o'};
namespace quicx {
namespace {

class MockTransport:
    public quicx::TlsHandlerInterface {
public:
    enum Role {
        R_CLITNE,
        R_SERVER,
    };

    MockTransport(Role role): _role(role) {
        // The caller is expected to configure initial secrets.
        _levels[EL_INITIAL].write_secret = {1};
        _levels[EL_INITIAL].read_secret = {1};
    }

    void SetPeer(MockTransport *peer) { _peer = peer; }

    bool HasAlert() const { return _has_alert; }
    EncryptionLevel AlertLevel() const { return _alert_level; }
    uint8_t Alert() const { return _alert; }

    bool PeerSecretsMatch(EncryptionLevel level) const {
        return _levels[level].write_secret == _peer->_levels[level].read_secret &&
            _levels[level].read_secret == _peer->_levels[level].write_secret &&
            _levels[level].cipher == _peer->_levels[level].cipher;
    }

    bool HasReadSecret(EncryptionLevel level) const {
        return !_levels[level].read_secret.empty();
    }

    bool HasWriteSecret(EncryptionLevel level) const {
        return !_levels[level].write_secret.empty();
    }

    void SetReadSecret(SSL* ssl, EncryptionLevel level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len) {
        if (HasReadSecret(level)) {
            ADD_FAILURE() << LevelToString(level) << " read secret configured twice";
            return;
        }

        if (_role == Role::R_CLITNE && level == EL_EARLY_DATA) {
            ADD_FAILURE() << "Unexpected early data read secret";
            return;
        }

        EncryptionLevel ack_level =
        level == EL_EARLY_DATA ? EL_APPLICATION : level;
        if (!HasWriteSecret(ack_level)) {
            ADD_FAILURE() << LevelToString(level) << " read secret configured before ACK write secret";
            return;
        }

        if (cipher == nullptr) {
            ADD_FAILURE() << "Unexpected null cipher";
            return;
        }

        if (level != EL_EARLY_DATA && SSL_CIPHER_get_id(cipher) != _levels[level].cipher) {
            ADD_FAILURE() << "Cipher suite inconsistent";
            return;
        }

        _levels[level].read_secret.resize(secret_len);
        memcpy(&(*_levels[level].read_secret.begin()), secret, secret_len);
        _levels[level].cipher = SSL_CIPHER_get_id(cipher);
    }

    void SetWriteSecret(SSL* ssl, EncryptionLevel level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len) {
        if (HasWriteSecret(level)) {
            ADD_FAILURE() << LevelToString(level) << " write secret configured twice";
            return;
        }

        if (_role == Role::R_SERVER && level == EL_EARLY_DATA) {
            ADD_FAILURE() << "Unexpected early data write secret";
            return;
        }

        if (cipher == nullptr) {
            ADD_FAILURE() << "Unexpected null cipher";
            return;
        }

        _levels[level].write_secret.resize(secret_len);
        memcpy(&(*_levels[level].write_secret.begin()), secret, secret_len);
        _levels[level].cipher = SSL_CIPHER_get_id(cipher);
        return;
    }

    void WriteMessage(EncryptionLevel level, const uint8_t *data,
        size_t len) {
        if (_levels[level].write_secret.empty()) {
            ADD_FAILURE() << LevelToString(level)
                    << " write secret not yet configured";
            return;
        }

        switch (level) {
            case EL_EARLY_DATA:
                ADD_FAILURE() << "unexpected handshake data at early data level";
                return;
            case EL_INITIAL:
                if (!_levels[EL_HANDSHAKE].write_secret.empty()) {
                    ADD_FAILURE() << LevelToString(level) << " handshake data written after handshake keys installed";
                    return;
                }
          case EL_HANDSHAKE:
                if (!_levels[EL_APPLICATION].write_secret.empty()) {
                    ADD_FAILURE() << LevelToString(level) << " handshake data written after application keys installed";
                    return;
                }
          case EL_APPLICATION:
            break;
        }
    
        std::vector<uint8_t> tempData(len);
        memcpy(&(*tempData.begin()), data, len);
        _levels[level].write_data.insert(_levels[level].write_data.end(), tempData.begin(), tempData.end());
    }

    void FlushFlight() {
        // do nothing
    }

    void SendAlert(EncryptionLevel level, uint8_t alert) {
        if (_has_alert) {
            ADD_FAILURE() << "duplicate alert sent";
            return;
        }

        if (_levels[level].write_secret.empty()) {
            ADD_FAILURE() << LevelToString(level) << " write secret not yet configured";
            return;
        }

        _has_alert = true;
        _alert_level = level;
        _alert = alert;
    }

    
    bool ReadHandshakeData(std::vector<uint8_t> *out, EncryptionLevel level, size_t num = std::numeric_limits<size_t>::max()) {
        if (_levels[level].read_secret.empty()) {
            ADD_FAILURE() << "data read before keys configured in level " << level;
            return false;
        }
        // The peer may not have configured any keys yet.
        if (_peer->_levels[level].write_secret.empty()) {
            out->clear();
            return true;
        }
      
        // Check the peer computed the same key.
        if (_peer->_levels[level].write_secret != _levels[level].read_secret) {
            ADD_FAILURE() << "peer write key does not match read key in level "
                      << level;
            return false;
        }

        if (_peer->_levels[level].cipher != _levels[level].cipher) {
            ADD_FAILURE() << "peer cipher does not match in level " << level;
            return false;
        }
      
        std::vector<uint8_t> *peer_data = &_peer->_levels[level].write_data;
        num = std::min(num, peer_data->size());
        out->assign(peer_data->begin(), peer_data->begin() + num);
        peer_data->erase(peer_data->begin(), peer_data->begin() + num);
        return true;
    }

private:
    const char* LevelToString(EncryptionLevel level) {
        switch (level) {
        case EL_INITIAL:
            return "initial";
        case EL_EARLY_DATA:
            return "early data";
        case EL_HANDSHAKE:
            return "handshake";
        case EL_APPLICATION:
            return "application";
        }
        return "<unknown>";
    }

private:
    Role _role;
    MockTransport *_peer = nullptr;

    bool _has_alert = false;
    EncryptionLevel _alert_level = EL_INITIAL;
    uint8_t _alert = 0;

    struct Level {
        std::vector<uint8_t> write_data;
        std::vector<uint8_t> write_secret;
        std::vector<uint8_t> read_secret;
        uint32_t cipher = 0;
    };
    Level _levels[NUM_ENCRYPTION_LEVELS];
};

class TestServerHandler:
    public quicx::TlsServerHandlerInterface {
public:    
    virtual void SSLAlpnSelect(const unsigned char **out, unsigned char *outlen,
        const unsigned char *in, unsigned int inlen, void *arg) {
        SSL_select_next_proto(const_cast<uint8_t **>(out), outlen, in, inlen,
                     kALPNProtos, sizeof(kALPNProtos));
    }
};

static bool ProvideHandshakeData(std::shared_ptr<MockTransport> mt, std::shared_ptr<quicx::TLSConnection> conn,  size_t num = std::numeric_limits<size_t>::max()) {
    EncryptionLevel level = conn->GetLevel();
    std::vector<uint8_t> data;
    return mt->ReadHandshakeData(&data, level, num) && conn->ProcessCryptoData(data.data(), data.size());
}

static const char __cert_pem[] =
      "-----BEGIN CERTIFICATE-----\n"
      "MIICWDCCAcGgAwIBAgIJAPuwTC6rEJsMMA0GCSqGSIb3DQEBBQUAMEUxCzAJBgNV\n"
      "BAYTAkFVMRMwEQYDVQQIDApTb21lLVN0YXRlMSEwHwYDVQQKDBhJbnRlcm5ldCBX\n"
      "aWRnaXRzIFB0eSBMdGQwHhcNMTQwNDIzMjA1MDQwWhcNMTcwNDIyMjA1MDQwWjBF\n"
      "MQswCQYDVQQGEwJBVTETMBEGA1UECAwKU29tZS1TdGF0ZTEhMB8GA1UECgwYSW50\n"
      "ZXJuZXQgV2lkZ2l0cyBQdHkgTHRkMIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKB\n"
      "gQDYK8imMuRi/03z0K1Zi0WnvfFHvwlYeyK9Na6XJYaUoIDAtB92kWdGMdAQhLci\n"
      "HnAjkXLI6W15OoV3gA/ElRZ1xUpxTMhjP6PyY5wqT5r6y8FxbiiFKKAnHmUcrgfV\n"
      "W28tQ+0rkLGMryRtrukXOgXBv7gcrmU7G1jC2a7WqmeI8QIDAQABo1AwTjAdBgNV\n"
      "HQ4EFgQUi3XVrMsIvg4fZbf6Vr5sp3Xaha8wHwYDVR0jBBgwFoAUi3XVrMsIvg4f\n"
      "Zbf6Vr5sp3Xaha8wDAYDVR0TBAUwAwEB/zANBgkqhkiG9w0BAQUFAAOBgQA76Hht\n"
      "ldY9avcTGSwbwoiuIqv0jTL1fHFnzy3RHMLDh+Lpvolc5DSrSJHCP5WuK0eeJXhr\n"
      "T5oQpHL9z/cCDLAKCKRa4uV0fhEdOWBqyR9p8y5jJtye72t6CuFUV5iqcpF4BH4f\n"
      "j2VNHwsSrJwkD4QUGlUtH7vwnQmyCFxZMmWAJg==\n"
      "-----END CERTIFICATE-----\n";

static const char __key_pem[] =
      "-----BEGIN RSA PRIVATE KEY-----\n"
      "MIICXgIBAAKBgQDYK8imMuRi/03z0K1Zi0WnvfFHvwlYeyK9Na6XJYaUoIDAtB92\n"
      "kWdGMdAQhLciHnAjkXLI6W15OoV3gA/ElRZ1xUpxTMhjP6PyY5wqT5r6y8FxbiiF\n"
      "KKAnHmUcrgfVW28tQ+0rkLGMryRtrukXOgXBv7gcrmU7G1jC2a7WqmeI8QIDAQAB\n"
      "AoGBAIBy09Fd4DOq/Ijp8HeKuCMKTHqTW1xGHshLQ6jwVV2vWZIn9aIgmDsvkjCe\n"
      "i6ssZvnbjVcwzSoByhjN8ZCf/i15HECWDFFh6gt0P5z0MnChwzZmvatV/FXCT0j+\n"
      "WmGNB/gkehKjGXLLcjTb6dRYVJSCZhVuOLLcbWIV10gggJQBAkEA8S8sGe4ezyyZ\n"
      "m4e9r95g6s43kPqtj5rewTsUxt+2n4eVodD+ZUlCULWVNAFLkYRTBCASlSrm9Xhj\n"
      "QpmWAHJUkQJBAOVzQdFUaewLtdOJoPCtpYoY1zd22eae8TQEmpGOR11L6kbxLQsk\n"
      "aMly/DOnOaa82tqAGTdqDEZgSNmCeKKknmECQAvpnY8GUOVAubGR6c+W90iBuQLj\n"
      "LtFp/9ihd2w/PoDwrHZaoUYVcT4VSfJQog/k7kjE4MYXYWL8eEKg3WTWQNECQQDk\n"
      "104Wi91Umd1PzF0ijd2jXOERJU1wEKe6XLkYYNHWQAe5l4J4MWj9OdxFXAxIuuR/\n"
      "tfDwbqkta4xcux67//khAkEAvvRXLHTaa6VFzTaiiO8SaFsHV3lQyXOtMrBpB5jd\n"
      "moZWgjHvB2W9Ckn7sDqsPB+U2tyX0joDdQEyuiMECDY8oQ==\n"
      "-----END RSA PRIVATE KEY-----\n"; 

TEST(crypto_ssl_connection_utest, test1) {
    std::shared_ptr<quicx::TLSCtx> client_ctx = std::make_shared<quicx::TLSClientCtx>();
    client_ctx->Init();

    BIO* cert_bio = BIO_new_mem_buf(__cert_pem, strlen(__cert_pem));
    EXPECT_TRUE(cert_bio != nullptr);
    quicx::CSmartPtr<X509, X509_free> cert = PEM_read_bio_X509(cert_bio, nullptr, nullptr, nullptr);
    EXPECT_TRUE(cert != nullptr);
    BIO_free(cert_bio);

    BIO* key_bio = BIO_new_mem_buf(__key_pem, strlen(__key_pem));
    EXPECT_TRUE(key_bio != nullptr);
    quicx::CSmartPtr<EVP_PKEY, EVP_PKEY_free> key = PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr);
    EXPECT_TRUE(key != nullptr);
    BIO_free(key_bio);

    std::shared_ptr<quicx::TLSServerCtx> server_ctx = std::make_shared<quicx::TLSServerCtx>();
    server_ctx->Init(cert.get(), key.get());

    std::shared_ptr<MockTransport> cli_handler = std::make_shared<MockTransport>(MockTransport::Role::R_CLITNE);
    std::shared_ptr<MockTransport> ser_handler = std::make_shared<MockTransport>(MockTransport::Role::R_SERVER);
    std::shared_ptr<TestServerHandler> ser_alpn_handler = std::make_shared<TestServerHandler>();

    ser_handler->SetPeer(cli_handler.get());
    cli_handler->SetPeer(ser_handler.get());

    std::shared_ptr<quicx::TLSClientConnection> cli_conn = std::make_shared<quicx::TLSClientConnection>(client_ctx, cli_handler);
    cli_conn->Init();

    std::shared_ptr<quicx::TLSServerConnection> ser_conn = std::make_shared<quicx::TLSServerConnection>(server_ctx, ser_handler, ser_alpn_handler);
    ser_conn->Init();

    static uint8_t client_transport_params[] = {0};
    cli_conn->AddTransportParam(client_transport_params, sizeof(client_transport_params));

    static uint8_t kALPNProtos[] = {'f', 'o', 'o'};
    cli_conn->AddAlpn(kALPNProtos, sizeof(kALPNProtos));

    static std::vector<uint8_t> server_transport_params = {1};
    ser_conn->AddTransportParam(server_transport_params.data(), server_transport_params.size());

    bool client_done = false, server_done = false;
    while (!client_done || !server_done) {
        if (!client_done) {
            if (!ProvideHandshakeData(cli_handler, cli_conn)) {
                ADD_FAILURE() << "ProvideHandshakeData client failed";
                return;
            }
            if (cli_conn->DoHandleShake()) {
                client_done = true;
            }
        }

        if (!server_done) {
            if (!ProvideHandshakeData(ser_handler, ser_conn)) {
                ADD_FAILURE() << "ProvideHandshakeData server failed";
                return;
            }
            if (ser_conn->DoHandleShake()) {
                server_done = true;
            }
        }
    }

    EXPECT_TRUE(ser_handler->PeerSecretsMatch(EL_APPLICATION));
    EXPECT_FALSE(ser_handler->HasAlert());
    EXPECT_FALSE(cli_handler->HasAlert());
}

}    
}