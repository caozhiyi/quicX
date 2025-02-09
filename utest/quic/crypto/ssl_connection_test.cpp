#include <gtest/gtest.h>
#include <openssl/bio.h>
#include <openssl/mem.h>
#include <openssl/pem.h>
#include "quic/crypto/type.h"
#include "quic/crypto/tls/type.h"
#include "common/util/c_smart_ptr.h"
#include "quic/crypto/tls/tls_ctx_client.h"
#include "quic/crypto/tls/tls_ctx_server.h"
#include "quic/crypto/tls/tls_conneciton_client.h"
#include "quic/crypto/tls/tls_conneciton_server.h"

static uint8_t kALPNProtos[] = {0x03, 'f', 'o', 'o'};
namespace quicx {
namespace quic {
namespace {

class MockTransport:
    public TlsHandlerInterface {
public:
    enum Role {
        R_CLITNE,
        R_SERVER,
    };

    MockTransport(Role role): role_(role) {
        // The caller is expected to configure initial secrets.
        levels_[kInitial].write_secret = {1};
        levels_[kInitial].read_secret = {1};
    }

    void SetPeer(MockTransport *peer) { peer_ = peer; }

    bool HasAlert() const { return has_alert_; }
    EncryptionLevel AlertLevel() const { return alert_level_; }
    uint8_t Alert() const { return alert_; }

    bool PeerSecretsMatch(EncryptionLevel level) const {
        return levels_[level].write_secret == peer_->levels_[level].read_secret &&
            levels_[level].read_secret == peer_->levels_[level].write_secret &&
            levels_[level].cipher == peer_->levels_[level].cipher;
    }

    bool HasReadSecret(EncryptionLevel level) const {
        return !levels_[level].read_secret.empty();
    }

    bool HasWriteSecret(EncryptionLevel level) const {
        return !levels_[level].write_secret.empty();
    }

    void SetReadSecret(SSL* ssl, EncryptionLevel level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len) {
        if (HasReadSecret(level)) {
            ADD_FAILURE() << LevelToString(level) << " read secret configured twice";
            return;
        }

        if (role_ == Role::R_CLITNE && level == kEarlyData) {
            ADD_FAILURE() << "Unexpected early data read secret";
            return;
        }

        EncryptionLevel ack_level =
        level == kEarlyData ? kApplication : level;
        if (!HasWriteSecret(ack_level)) {
            ADD_FAILURE() << LevelToString(level) << " read secret configured before ACK write secret";
            return;
        }

        if (cipher == nullptr) {
            ADD_FAILURE() << "Unexpected null cipher";
            return;
        }

        if (level != kEarlyData && SSL_CIPHER_get_id(cipher) != levels_[level].cipher) {
            ADD_FAILURE() << "Cipher suite inconsistent";
            return;
        }

        levels_[level].read_secret.resize(secret_len);
        memcpy(&(*levels_[level].read_secret.begin()), secret, secret_len);
        levels_[level].cipher = SSL_CIPHER_get_id(cipher);
    }

    void SetWriteSecret(SSL* ssl, EncryptionLevel level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len) {
        if (HasWriteSecret(level)) {
            ADD_FAILURE() << LevelToString(level) << " write secret configured twice";
            return;
        }

        if (role_ == Role::R_SERVER && level == kEarlyData) {
            ADD_FAILURE() << "Unexpected early data write secret";
            return;
        }

        if (cipher == nullptr) {
            ADD_FAILURE() << "Unexpected null cipher";
            return;
        }

        levels_[level].write_secret.resize(secret_len);
        memcpy(&(*levels_[level].write_secret.begin()), secret, secret_len);
        levels_[level].cipher = SSL_CIPHER_get_id(cipher);
        return;
    }

    void WriteMessage(EncryptionLevel level, const uint8_t *data,
        size_t len) {
        if (levels_[level].write_secret.empty()) {
            ADD_FAILURE() << LevelToString(level)
                    << " write secret not yet configured";
            return;
        }

        switch (level) {
            case kEarlyData:
                ADD_FAILURE() << "unexpected handshake data at early data level";
                return;
            case kInitial:
                if (!levels_[kHandshake].write_secret.empty()) {
                    ADD_FAILURE() << LevelToString(level) << " handshake data written after handshake keys installed";
                    return;
                }
            case kHandshake:
                if (!levels_[kApplication].write_secret.empty()) {
                    ADD_FAILURE() << LevelToString(level) << " handshake data written after application keys installed";
                    return;
                }
            case kApplication:
                break;
            default:
                break;
        }
    
        std::vector<uint8_t> tempData(len);
        memcpy(&(*tempData.begin()), data, len);
        levels_[level].write_data.insert(levels_[level].write_data.end(), tempData.begin(), tempData.end());
    }

    void FlushFlight() {
        // do nothing
    }

    void SendAlert(EncryptionLevel level, uint8_t alert) {
        if (has_alert_) {
            ADD_FAILURE() << "duplicate alert sent";
            return;
        }

        if (levels_[level].write_secret.empty()) {
            ADD_FAILURE() << LevelToString(level) << " write secret not yet configured";
            return;
        }

        has_alert_ = true;
        alert_level_ = level;
        alert_ = alert;
    }

    void OnTransportParams(EncryptionLevel level, const uint8_t* tp, size_t tp_len) {
        if (transport_param_done_) {
            return;
        }
        transport_param_done_ = true;
    }
    
    bool ReadHandshakeData(std::vector<uint8_t> *out, EncryptionLevel level, size_t num = std::numeric_limits<size_t>::max()) {
        if (levels_[level].read_secret.empty()) {
            ADD_FAILURE() << "data read before keys configured in level " << level;
            return false;
        }
        // The peer may not have configured any keys yet.
        if (peer_->levels_[level].write_secret.empty()) {
            out->clear();
            return true;
        }
      
        // Check the peer computed the same key.
        if (peer_->levels_[level].write_secret != levels_[level].read_secret) {
            ADD_FAILURE() << "peer write key does not match read key in level "
                      << level;
            return false;
        }

        if (peer_->levels_[level].cipher != levels_[level].cipher) {
            ADD_FAILURE() << "peer cipher does not match in level " << level;
            return false;
        }
      
        std::vector<uint8_t> *peer_data = &peer_->levels_[level].write_data;
        num = std::min(num, peer_data->size());
        out->assign(peer_data->begin(), peer_data->begin() + num);
        peer_data->erase(peer_data->begin(), peer_data->begin() + num);
        return true;
    }

private:
    const char* LevelToString(EncryptionLevel level) {
        switch (level) {
        case kInitial:
            return "initial";
        case kEarlyData:
            return "early data";
        case kHandshake:
            return "handshake";
        case kApplication:
            return "application";
        default:
            break;
        }
        return "<unknown>";
    }

private:
    Role role_;
    bool transport_param_done_ = false;
    MockTransport *peer_ = nullptr;

    bool has_alert_ = false;
    EncryptionLevel alert_level_ = kInitial;
    uint8_t alert_ = 0;

    struct Level {
        std::vector<uint8_t> write_data;
        std::vector<uint8_t> write_secret;
        std::vector<uint8_t> read_secret;
        uint32_t cipher = 0;
    };
    Level levels_[kNumEncryptionLevels];
};

class TestServerHandler:
    public TlsServerHandlerInterface {
public:    
    virtual void SSLAlpnSelect(const unsigned char **out, unsigned char *outlen,
        const unsigned char *in, unsigned int inlen, void *arg) {
        SSL_select_next_proto(const_cast<uint8_t **>(out), outlen, in, inlen,
                     kALPNProtos, sizeof(kALPNProtos));
    }
};

static bool ProvideHandshakeData(std::shared_ptr<MockTransport> mt, std::shared_ptr<TLSConnection> conn,  size_t num = std::numeric_limits<size_t>::max()) {
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
    std::shared_ptr<TLSCtx> client_ctx = std::make_shared<TLSClientCtx>();
    client_ctx->Init();

    std::shared_ptr<TLSServerCtx> server_ctx = std::make_shared<TLSServerCtx>();
    server_ctx->Init(__cert_pem, __key_pem);

    std::shared_ptr<MockTransport> cli_handler = std::make_shared<MockTransport>(MockTransport::Role::R_CLITNE);
    std::shared_ptr<MockTransport> ser_handler = std::make_shared<MockTransport>(MockTransport::Role::R_SERVER);
    std::shared_ptr<TestServerHandler> ser_alpn_handler = std::make_shared<TestServerHandler>();

    ser_handler->SetPeer(cli_handler.get());
    cli_handler->SetPeer(ser_handler.get());

    std::shared_ptr<TLSClientConnection> cli_conn = std::make_shared<TLSClientConnection>(client_ctx, cli_handler.get());
    cli_conn->Init();

    std::shared_ptr<TLSServerConnection> ser_conn = std::make_shared<TLSServerConnection>(server_ctx, ser_handler.get(), ser_alpn_handler.get());
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

    EXPECT_TRUE(ser_handler->PeerSecretsMatch(kApplication));
    EXPECT_FALSE(ser_handler->HasAlert());
    EXPECT_FALSE(cli_handler->HasAlert());
}

}    
}
}