
#include <gtest/gtest.h>

#include <vector>
#include <cstring>

#include <openssl/ssl.h>
#include <openssl/crypto.h>
#include <openssl/bio.h>
#include <openssl/pem.h>

namespace quicx {
namespace quic {
namespace {

static bssl::UniquePtr<SSL_SESSION> g_last_session;

static int SaveLastSession(SSL *ssl, SSL_SESSION *session) {
  // Save the most recent session.
  g_last_session.reset(session);
  return 1;
}

static bssl::UniquePtr<X509> CertFromPEM(const char *pem) {
  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(pem, strlen(pem)));
  if (!bio) {
    return nullptr;
  }
  return bssl::UniquePtr<X509>(
      PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
}

static bssl::UniquePtr<EVP_PKEY> KeyFromPEM(const char *pem) {
  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(pem, strlen(pem)));
  if (!bio) {
    return nullptr;
  }
  return bssl::UniquePtr<EVP_PKEY>(
      PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
}

static bssl::UniquePtr<X509> GetTestCertificate() {
  static const char kCertPEM[] =
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
  return CertFromPEM(kCertPEM);
}

static bssl::UniquePtr<EVP_PKEY> GetTestKey() {
  static const char kKeyPEM[] =
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
  return KeyFromPEM(kKeyPEM);
}

static bssl::UniquePtr<SSL_CTX> CreateContextWithTestCertificate(
    const SSL_METHOD *method) {
  bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(method));
  bssl::UniquePtr<X509> cert = GetTestCertificate();
  bssl::UniquePtr<EVP_PKEY> key = GetTestKey();
  if (!ctx || !cert || !key ||
      !SSL_CTX_use_certificate(ctx.get(), cert.get()) ||
      !SSL_CTX_use_PrivateKey(ctx.get(), key.get())) {
    return nullptr;
  }
  return ctx;
}

const char *LevelToString(ssl_encryption_level_t level) {
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
constexpr size_t kNumQUICLevels = 4;

class MockQUICTransport {
 public:
  enum class Role { kClient, kServer };

  explicit MockQUICTransport(Role role) : role_(role) {
    // The caller is expected to configure initial secrets.
    levels_[ssl_encryption_initial].write_secret = {1};
    levels_[ssl_encryption_initial].read_secret = {1};
  }

  void set_peer(MockQUICTransport *peer) { peer_ = peer; }

  bool has_alert() const { return has_alert_; }
  ssl_encryption_level_t alert_level() const { return alert_level_; }
  uint8_t alert() const { return alert_; }

  bool PeerSecretsMatch(ssl_encryption_level_t level) const {
    return levels_[level].write_secret == peer_->levels_[level].read_secret &&
           levels_[level].read_secret == peer_->levels_[level].write_secret &&
           levels_[level].cipher == peer_->levels_[level].cipher;
  }

  bool HasReadSecret(ssl_encryption_level_t level) const {
    return !levels_[level].read_secret.empty();
  }

  bool HasWriteSecret(ssl_encryption_level_t level) const {
    return !levels_[level].write_secret.empty();
  }

  void AllowOutOfOrderWrites() { allow_out_of_order_writes_ = true; }

  bool SetReadSecret(ssl_encryption_level_t level, const SSL_CIPHER *cipher,
                     bssl::Span<const uint8_t> secret) {
    if (HasReadSecret(level)) {
      ADD_FAILURE() << LevelToString(level) << " read secret configured twice";
      return false;
    }

    if (role_ == Role::kClient && level == ssl_encryption_early_data) {
      ADD_FAILURE() << "Unexpected early data read secret";
      return false;
    }

    ssl_encryption_level_t ack_level =
        level == ssl_encryption_early_data ? ssl_encryption_application : level;
    if (!HasWriteSecret(ack_level)) {
      ADD_FAILURE() << LevelToString(level)
                    << " read secret configured before ACK write secret";
      return false;
    }

    if (cipher == nullptr) {
      ADD_FAILURE() << "Unexpected null cipher";
      return false;
    }

    if (level != ssl_encryption_early_data &&
        SSL_CIPHER_get_id(cipher) != levels_[level].cipher) {
      ADD_FAILURE() << "Cipher suite inconsistent";
      return false;
    }

    levels_[level].read_secret.assign(secret.begin(), secret.end());
    levels_[level].cipher = SSL_CIPHER_get_id(cipher);
    return true;
  }

  bool SetWriteSecret(ssl_encryption_level_t level, const SSL_CIPHER *cipher,
                      bssl::Span<const uint8_t> secret) {
    if (HasWriteSecret(level)) {
      ADD_FAILURE() << LevelToString(level) << " write secret configured twice";
      return false;
    }

    if (role_ == Role::kServer && level == ssl_encryption_early_data) {
      ADD_FAILURE() << "Unexpected early data write secret";
      return false;
    }

    if (cipher == nullptr) {
      ADD_FAILURE() << "Unexpected null cipher";
      return false;
    }

    levels_[level].write_secret.assign(secret.begin(), secret.end());
    levels_[level].cipher = SSL_CIPHER_get_id(cipher);
    return true;
  }

  bool WriteHandshakeData(ssl_encryption_level_t level,
                          bssl::Span<const uint8_t> data) {
    if (levels_[level].write_secret.empty()) {
      ADD_FAILURE() << LevelToString(level)
                    << " write secret not yet configured";
      return false;
    }

    // Although the levels are conceptually separate, BoringSSL finishes writing
    // data from a previous level before installing keys for the next level.
    if (!allow_out_of_order_writes_) {
      switch (level) {
        case ssl_encryption_early_data:
          ADD_FAILURE() << "unexpected handshake data at early data level";
          return false;
        case ssl_encryption_initial:
          if (!levels_[ssl_encryption_handshake].write_secret.empty()) {
            ADD_FAILURE()
                << LevelToString(level)
                << " handshake data written after handshake keys installed";
            return false;
          }
          [[fallthrough]];
        case ssl_encryption_handshake:
          if (!levels_[ssl_encryption_application].write_secret.empty()) {
            ADD_FAILURE()
                << LevelToString(level)
                << " handshake data written after application keys installed";
            return false;
          }
          [[fallthrough]];
        case ssl_encryption_application:
          break;
      }
    }

    levels_[level].write_data.insert(levels_[level].write_data.end(),
                                     data.begin(), data.end());
    return true;
  }

  bool SendAlert(ssl_encryption_level_t level, uint8_t alert_value) {
    if (has_alert_) {
      ADD_FAILURE() << "duplicate alert sent";
      return false;
    }

    if (levels_[level].write_secret.empty()) {
      ADD_FAILURE() << LevelToString(level)
                    << " write secret not yet configured";
      return false;
    }

    has_alert_ = true;
    alert_level_ = level;
    alert_ = alert_value;
    return true;
  }

  bool ReadHandshakeData(std::vector<uint8_t> *out,
                         ssl_encryption_level_t level,
                         size_t num = std::numeric_limits<size_t>::max()) {
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
  Role role_;
  MockQUICTransport *peer_ = nullptr;

  bool allow_out_of_order_writes_ = false;
  bool has_alert_ = false;
  ssl_encryption_level_t alert_level_ = ssl_encryption_initial;
  uint8_t alert_ = 0;

  struct Level {
    std::vector<uint8_t> write_data;
    std::vector<uint8_t> write_secret;
    std::vector<uint8_t> read_secret;
    uint32_t cipher = 0;
  };
  Level levels_[kNumQUICLevels];
};

class MockQUICTransportPair {
 public:
  MockQUICTransportPair()
      : client_(MockQUICTransport::Role::kClient),
        server_(MockQUICTransport::Role::kServer) {
    client_.set_peer(&server_);
    server_.set_peer(&client_);
  }

  ~MockQUICTransportPair() {
    client_.set_peer(nullptr);
    server_.set_peer(nullptr);
  }

  MockQUICTransport *client() { return &client_; }
  MockQUICTransport *server() { return &server_; }

  bool SecretsMatch(ssl_encryption_level_t level) const {
    // We only need to check |HasReadSecret| and |HasWriteSecret| on |client_|.
    // |PeerSecretsMatch| checks that |server_| is analogously configured.
    return client_.PeerSecretsMatch(level) && client_.HasWriteSecret(level) &&
           (level == ssl_encryption_early_data || client_.HasReadSecret(level));
  }

 private:
  MockQUICTransport client_;
  MockQUICTransport server_;
};

template <typename T>
class UnownedSSLExData {
 public:
  UnownedSSLExData() {
    index_ = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
  }

  T *Get(const SSL *ssl) {
    return index_ < 0 ? nullptr
                      : static_cast<T *>(SSL_get_ex_data(ssl, index_));
  }

  bool Set(SSL *ssl, T *t) {
    return index_ >= 0 && SSL_set_ex_data(ssl, index_, t);
  }

 private:
  int index_;
};

class QUICMethodTest : public testing::Test {
 protected:
  void SetUp() override {
    client_ctx_.reset(SSL_CTX_new(TLS_method()));
    server_ctx_ = CreateContextWithTestCertificate(TLS_method());
    ASSERT_TRUE(client_ctx_);
    ASSERT_TRUE(server_ctx_);

    SSL_CTX_set_min_proto_version(server_ctx_.get(), TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(server_ctx_.get(), TLS1_3_VERSION);
    SSL_CTX_set_min_proto_version(client_ctx_.get(), TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(client_ctx_.get(), TLS1_3_VERSION);

    static const uint8_t kALPNProtos[] = {0x03, 'f', 'o', 'o'};
    ASSERT_EQ(SSL_CTX_set_alpn_protos(client_ctx_.get(), kALPNProtos,
                                      sizeof(kALPNProtos)),
              0);
    SSL_CTX_set_alpn_select_cb(
        server_ctx_.get(),
        [](SSL *ssl, const uint8_t **out, uint8_t *out_len, const uint8_t *in,
           unsigned in_len, void *arg) -> int {
          return SSL_select_next_proto(
                     const_cast<uint8_t **>(out), out_len, in, in_len,
                     kALPNProtos, sizeof(kALPNProtos)) == OPENSSL_NPN_NEGOTIATED
                     ? SSL_TLSEXT_ERR_OK
                     : SSL_TLSEXT_ERR_NOACK;
        },
        nullptr);
  }

  static MockQUICTransport *TransportFromSSL(const SSL *ssl) {
    return ex_data_.Get(ssl);
  }

  static bool ProvideHandshakeData(
      SSL *ssl, size_t num = std::numeric_limits<size_t>::max()) {
    MockQUICTransport *transport = TransportFromSSL(ssl);
    ssl_encryption_level_t level = SSL_quic_read_level(ssl);
    std::vector<uint8_t> data;
    return transport->ReadHandshakeData(&data, level, num) &&
           SSL_provide_quic_data(ssl, level, data.data(), data.size());
  }

  void AllowOutOfOrderWrites() { allow_out_of_order_writes_ = true; }

  bool CreateClientAndServer() {
    client_.reset(SSL_new(client_ctx_.get()));
    server_.reset(SSL_new(server_ctx_.get()));
    if (!client_ || !server_) {
      return false;
    }

    SSL_set_connect_state(client_.get());
    SSL_set_accept_state(server_.get());

    transport_ = std::make_unique<MockQUICTransportPair>();
    if (!ex_data_.Set(client_.get(), transport_->client()) ||
        !ex_data_.Set(server_.get(), transport_->server())) {
      return false;
    }
    if (allow_out_of_order_writes_) {
      transport_->client()->AllowOutOfOrderWrites();
      transport_->server()->AllowOutOfOrderWrites();
    }
    static const uint8_t client_transport_params[] = {0};
    if (!SSL_set_quic_transport_params(client_.get(), client_transport_params,
                                       sizeof(client_transport_params)) ||
        !SSL_set_quic_transport_params(server_.get(),
                                       server_transport_params_.data(),
                                       server_transport_params_.size()) ||
        !SSL_set_quic_early_data_context(
            server_.get(), server_quic_early_data_context_.data(),
            server_quic_early_data_context_.size())) {
      return false;
    }
    return true;
  }

  enum class ExpectedError {
    kNoError,
    kClientError,
    kServerError,
  };

  // CompleteHandshakesForQUIC runs |SSL_do_handshake| on |client_| and
  // |server_| until each completes once. It returns true on success and false
  // on failure.
  bool CompleteHandshakesForQUIC() {
    return RunQUICHandshakesAndExpectError(ExpectedError::kNoError);
  }

  // Runs |SSL_do_handshake| on |client_| and |server_| until each completes
  // once. If |expect_client_error| is true, it will return true only if the
  // client handshake failed. Otherwise, it returns true if both handshakes
  // succeed and false otherwise.
  bool RunQUICHandshakesAndExpectError(ExpectedError expected_error) {
    bool client_done = false, server_done = false;
    while (!client_done || !server_done) {
      if (!client_done) {
        if (!ProvideHandshakeData(client_.get())) {
          ADD_FAILURE() << "ProvideHandshakeData(client_) failed";
          return false;
        }
        int client_ret = SSL_do_handshake(client_.get());
        int client_err = SSL_get_error(client_.get(), client_ret);
        if (client_ret == 1) {
          client_done = true;
        } else if (client_ret != -1 || client_err != SSL_ERROR_WANT_READ) {
          if (expected_error == ExpectedError::kClientError) {
            return true;
          }
          ADD_FAILURE() << "Unexpected client output: " << client_ret << " "
                        << client_err;
          return false;
        }
      }

      if (!server_done) {
        if (!ProvideHandshakeData(server_.get())) {
          ADD_FAILURE() << "ProvideHandshakeData(server_) failed";
          return false;
        }
        int server_ret = SSL_do_handshake(server_.get());
        int server_err = SSL_get_error(server_.get(), server_ret);
        if (server_ret == 1) {
          server_done = true;
        } else if (server_ret != -1 || server_err != SSL_ERROR_WANT_READ) {
          if (expected_error == ExpectedError::kServerError) {
            return true;
          }
          ADD_FAILURE() << "Unexpected server output: " << server_ret << " "
                        << server_err;
          return false;
        }
      }
    }
    return expected_error == ExpectedError::kNoError;
  }

  bssl::UniquePtr<SSL_SESSION> CreateClientSessionForQUIC() {
    g_last_session = nullptr;
    SSL_CTX_sess_set_new_cb(client_ctx_.get(), SaveLastSession);
    if (!CreateClientAndServer() || !CompleteHandshakesForQUIC()) {
      return nullptr;
    }

    // The server sent NewSessionTicket messages in the handshake.
    if (!ProvideHandshakeData(client_.get()) ||
        !SSL_process_quic_post_handshake(client_.get())) {
      return nullptr;
    }

    return std::move(g_last_session);
  }

  void ExpectHandshakeSuccess() {
    EXPECT_TRUE(transport_->SecretsMatch(ssl_encryption_application));
    EXPECT_EQ(ssl_encryption_application, SSL_quic_read_level(client_.get()));
    EXPECT_EQ(ssl_encryption_application, SSL_quic_write_level(client_.get()));
    EXPECT_EQ(ssl_encryption_application, SSL_quic_read_level(server_.get()));
    EXPECT_EQ(ssl_encryption_application, SSL_quic_write_level(server_.get()));
    EXPECT_FALSE(transport_->client()->has_alert());
    EXPECT_FALSE(transport_->server()->has_alert());

    // SSL_do_handshake is now idempotent.
    EXPECT_EQ(SSL_do_handshake(client_.get()), 1);
    EXPECT_EQ(SSL_do_handshake(server_.get()), 1);
  }

  // Returns a default SSL_QUIC_METHOD. Individual methods may be overwritten by
  // the test.
  SSL_QUIC_METHOD DefaultQUICMethod() {
    return SSL_QUIC_METHOD{
        SetReadSecretCallback, SetWriteSecretCallback, AddHandshakeDataCallback,
        FlushFlightCallback,   SendAlertCallback,
    };
  }

  static int SetReadSecretCallback(SSL *ssl, ssl_encryption_level_t level,
                                   const SSL_CIPHER *cipher,
                                   const uint8_t *secret, size_t secret_len) {
    return TransportFromSSL(ssl)->SetReadSecret(level, cipher,
                                                bssl::Span(secret, secret_len));
  }

  static int SetWriteSecretCallback(SSL *ssl, ssl_encryption_level_t level,
                                    const SSL_CIPHER *cipher,
                                    const uint8_t *secret, size_t secret_len) {
    return TransportFromSSL(ssl)->SetWriteSecret(level, cipher,
                                                 bssl::Span(secret, secret_len));
  }

  static int AddHandshakeDataCallback(SSL *ssl,
                                      enum ssl_encryption_level_t level,
                                      const uint8_t *data, size_t len) {
    EXPECT_EQ(level, SSL_quic_write_level(ssl));
    return TransportFromSSL(ssl)->WriteHandshakeData(level, bssl::Span(data, len));
  }

  static int FlushFlightCallback(SSL *ssl) { return 1; }

  static int SendAlertCallback(SSL *ssl, ssl_encryption_level_t level,
                               uint8_t alert) {
    EXPECT_EQ(level, SSL_quic_write_level(ssl));
    return TransportFromSSL(ssl)->SendAlert(level, alert);
  }

  bssl::UniquePtr<SSL_CTX> client_ctx_;
  bssl::UniquePtr<SSL_CTX> server_ctx_;

  static UnownedSSLExData<MockQUICTransport> ex_data_;
  std::unique_ptr<MockQUICTransportPair> transport_;

  bssl::UniquePtr<SSL> client_;
  bssl::UniquePtr<SSL> server_;

  std::vector<uint8_t> server_transport_params_ = {1};
  std::vector<uint8_t> server_quic_early_data_context_ = {2};

  bool allow_out_of_order_writes_ = false;
};
UnownedSSLExData<MockQUICTransport> QUICMethodTest::ex_data_;

TEST_F(QUICMethodTest, ZeroRTTAccept) {
  const SSL_QUIC_METHOD quic_method = DefaultQUICMethod();

  SSL_CTX_set_session_cache_mode(client_ctx_.get(), SSL_SESS_CACHE_BOTH);
  SSL_CTX_set_early_data_enabled(client_ctx_.get(), 1);
  SSL_CTX_set_early_data_enabled(server_ctx_.get(), 1);
  ASSERT_TRUE(SSL_CTX_set_quic_method(client_ctx_.get(), &quic_method));
  ASSERT_TRUE(SSL_CTX_set_quic_method(server_ctx_.get(), &quic_method));

  bssl::UniquePtr<SSL_SESSION> session = CreateClientSessionForQUIC();
  ASSERT_TRUE(session);

  ASSERT_TRUE(CreateClientAndServer());
  SSL_set_session(client_.get(), session.get());

  // The client handshake should return immediately into the early data state.
  ASSERT_EQ(SSL_do_handshake(client_.get()), 1);
  EXPECT_TRUE(SSL_in_early_data(client_.get()));
  // The transport should have keys for sending 0-RTT data.
  EXPECT_TRUE(transport_->client()->HasWriteSecret(ssl_encryption_early_data));

  // The server will consume the ClientHello and also enter the early data
  // state.
  ASSERT_TRUE(ProvideHandshakeData(server_.get()));
  ASSERT_EQ(SSL_do_handshake(server_.get()), 1);
  EXPECT_TRUE(SSL_in_early_data(server_.get()));
  EXPECT_TRUE(transport_->SecretsMatch(ssl_encryption_early_data));
  // At this point, the server has half-RTT write keys, but it cannot access
  // 1-RTT read keys until client Finished.
  EXPECT_TRUE(transport_->server()->HasWriteSecret(ssl_encryption_application));
  EXPECT_FALSE(transport_->server()->HasReadSecret(ssl_encryption_application));

  // Finish up the client and server handshakes.
  ASSERT_TRUE(CompleteHandshakesForQUIC());

  // Both sides can now exchange 1-RTT data.
  ExpectHandshakeSuccess();
  EXPECT_TRUE(SSL_session_reused(client_.get()));
  EXPECT_TRUE(SSL_session_reused(server_.get()));
  EXPECT_FALSE(SSL_in_early_data(client_.get()));
  EXPECT_FALSE(SSL_in_early_data(server_.get()));
  EXPECT_TRUE(SSL_early_data_accepted(client_.get()));
  EXPECT_TRUE(SSL_early_data_accepted(server_.get()));

  // Finish handling post-handshake messages after the first 0-RTT resumption.
  EXPECT_TRUE(ProvideHandshakeData(client_.get()));
  EXPECT_TRUE(SSL_process_quic_post_handshake(client_.get()));

  // Perform a second 0-RTT resumption attempt, and confirm that 0-RTT is
  // accepted again.
  ASSERT_TRUE(CreateClientAndServer());
  SSL_set_session(client_.get(), g_last_session.get());

  // The client handshake should return immediately into the early data state.
  ASSERT_EQ(SSL_do_handshake(client_.get()), 1);
  EXPECT_TRUE(SSL_in_early_data(client_.get()));
  // The transport should have keys for sending 0-RTT data.
  EXPECT_TRUE(transport_->client()->HasWriteSecret(ssl_encryption_early_data));

  // The server will consume the ClientHello and also enter the early data
  // state.
  ASSERT_TRUE(ProvideHandshakeData(server_.get()));
  ASSERT_EQ(SSL_do_handshake(server_.get()), 1);
  EXPECT_TRUE(SSL_in_early_data(server_.get()));
  EXPECT_TRUE(transport_->SecretsMatch(ssl_encryption_early_data));
  // At this point, the server has half-RTT write keys, but it cannot access
  // 1-RTT read keys until client Finished.
  EXPECT_TRUE(transport_->server()->HasWriteSecret(ssl_encryption_application));
  EXPECT_FALSE(transport_->server()->HasReadSecret(ssl_encryption_application));

  // Finish up the client and server handshakes.
  ASSERT_TRUE(CompleteHandshakesForQUIC());

  // Both sides can now exchange 1-RTT data.
  ExpectHandshakeSuccess();
  EXPECT_TRUE(SSL_session_reused(client_.get()));
  EXPECT_TRUE(SSL_session_reused(server_.get()));
  EXPECT_FALSE(SSL_in_early_data(client_.get()));
  EXPECT_FALSE(SSL_in_early_data(server_.get()));
  EXPECT_TRUE(SSL_early_data_accepted(client_.get()));
  EXPECT_TRUE(SSL_early_data_accepted(server_.get()));
  EXPECT_EQ(SSL_get_early_data_reason(client_.get()), ssl_early_data_accepted);
  EXPECT_EQ(SSL_get_early_data_reason(server_.get()), ssl_early_data_accepted);
}

}  // namespace
}  // namespace quic
}  // namespace quicx