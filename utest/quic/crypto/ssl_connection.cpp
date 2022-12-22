#include <gtest/gtest.h>
#include "quic/crypto/tls/tls_client_ctx.h"
#include "quic/crypto/tls/tls_server_ctx.h"
#include "quic/crypto/tls/tls_client_conneciton.h"
#include "quic/crypto/tls/tls_server_conneciton.h"

constexpr size_t kNumQUICLevels = 4;
static uint8_t kALPNProtos[] = {0x03, 'f', 'o', 'o'};

class MockTransport:
    public quicx::TlsHandlerInterface {
public:
    enum Role {
        R_CLITNE,
        R_SERVER,
    };

    MockTransport(Role role): _role(role) {
        // The caller is expected to configure initial secrets.
        _levels[ssl_encryption_initial].write_secret = {1};
        _levels[ssl_encryption_initial].read_secret = {1};
    }

    void SetPeer(MockTransport *peer) { _peer = peer; }

    bool HasAlert() const { return _has_alert; }
    ssl_encryption_level_t AlertLevel() const { return _alert_level; }
    uint8_t Alert() const { return _alert; }

    bool PeerSecretsMatch(ssl_encryption_level_t level) const {
        return _levels[level].write_secret == _peer->_levels[level].read_secret &&
            _levels[level].read_secret == _peer->_levels[level].write_secret &&
            _levels[level].cipher == _peer->_levels[level].cipher;
    }

    bool HasReadSecret(ssl_encryption_level_t level) const {
        return !_levels[level].read_secret.empty();
    }

    bool HasWriteSecret(ssl_encryption_level_t level) const {
        return !_levels[level].write_secret.empty();
    }

    void SetReadSecret(SSL* ssl, ssl_encryption_level_t level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len) {
        if (HasReadSecret(level)) {
            ADD_FAILURE() << LevelToString(level) << " read secret configured twice";
            return;
        }

        if (_role == Role::R_CLITNE && level == ssl_encryption_early_data) {
            ADD_FAILURE() << "Unexpected early data read secret";
            return;
        }

        ssl_encryption_level_t ack_level =
        level == ssl_encryption_early_data ? ssl_encryption_application : level;
        if (!HasWriteSecret(ack_level)) {
            ADD_FAILURE() << LevelToString(level) << " read secret configured before ACK write secret";
            return;
        }

        if (cipher == nullptr) {
            ADD_FAILURE() << "Unexpected null cipher";
            return;
        }

        if (level != ssl_encryption_early_data && SSL_CIPHER_get_id(cipher) != _levels[level].cipher) {
            ADD_FAILURE() << "Cipher suite inconsistent";
            return;
        }

        _levels[level].read_secret.resize(secret_len);
        memcpy(&(*_levels[level].read_secret.begin()), secret, secret_len);
        _levels[level].cipher = SSL_CIPHER_get_id(cipher);
    }

    void SetWriteSecret(SSL* ssl, ssl_encryption_level_t level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len) {
        if (HasWriteSecret(level)) {
            ADD_FAILURE() << LevelToString(level) << " write secret configured twice";
            return;
        }

        if (_role == Role::R_SERVER && level == ssl_encryption_early_data) {
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

    void WriteMessage(ssl_encryption_level_t level, const uint8_t *data,
        size_t len) {
        if (_levels[level].write_secret.empty()) {
            ADD_FAILURE() << LevelToString(level)
                    << " write secret not yet configured";
            return;
        }

        switch (level) {
            case ssl_encryption_early_data:
                ADD_FAILURE() << "unexpected handshake data at early data level";
                return;
            case ssl_encryption_initial:
                if (!_levels[ssl_encryption_handshake].write_secret.empty()) {
                    ADD_FAILURE() << LevelToString(level) << " handshake data written after handshake keys installed";
                    return;
                }
          case ssl_encryption_handshake:
                if (!_levels[ssl_encryption_application].write_secret.empty()) {
                    ADD_FAILURE() << LevelToString(level) << " handshake data written after application keys installed";
                    return;
                }
          case ssl_encryption_application:
            break;
        }
    
        std::vector<uint8_t> tempData(len);
        memcpy(&(*tempData.begin()), data, len);
        _levels[level].write_data.insert(_levels[level].write_data.end(), tempData.begin(), tempData.end());
    }

    void FlushFlight() {
        // do nothing
    }

    void SendAlert(ssl_encryption_level_t level, uint8_t alert) {
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

    
    bool ReadHandshakeData(std::vector<uint8_t> *out, ssl_encryption_level_t level, size_t num = std::numeric_limits<size_t>::max()) {
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
    const char* LevelToString(ssl_encryption_level_t level) {
        switch (level) {
        case ssl_encryption_initial:
            return "initial";
        case ssl_encryption_early_data:
            return "early data";
        case ssl_encryption_handshake:
            return "handshake";
        case ssl_encryption_application:
            return "application";
        }
        return "<unknown>";
    }

private:
    Role _role;
    MockTransport *_peer = nullptr;

    bool _has_alert = false;
    ssl_encryption_level_t _alert_level = ssl_encryption_initial;
    uint8_t _alert = 0;

    struct Level {
        std::vector<uint8_t> write_data;
        std::vector<uint8_t> write_secret;
        std::vector<uint8_t> read_secret;
        uint32_t cipher = 0;
    };
    Level _levels[kNumQUICLevels];
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
    ssl_encryption_level_t level = conn->GetLevel();
    std::vector<uint8_t> data;
    return mt->ReadHandshakeData(&data, level, num) && conn->ProcessCryptoData(data.data(), data.size());
}

TEST(crypto_ssl_connection_utest, test1) {
    std::shared_ptr<quicx::TLSCtx> client_ctx = std::make_shared<quicx::TLSClientCtx>();
    client_ctx->Init();

    std::shared_ptr<quicx::TLSServerCtx> server_ctx = std::make_shared<quicx::TLSServerCtx>();
    server_ctx->Init("server.crt", "server.key");

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

    EXPECT_TRUE(ser_handler->PeerSecretsMatch(ssl_encryption_application));
    EXPECT_FALSE(ser_handler->HasAlert());
    EXPECT_FALSE(cli_handler->HasAlert());
}