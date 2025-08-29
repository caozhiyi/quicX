#include <gtest/gtest.h>

#include <vector>
#include <cstring>

#include <openssl/ssl.h>
#include <openssl/crypto.h>
#include <openssl/bio.h>
#include <openssl/pem.h>

// A minimal QUIC/TLS harness using only BoringSSL APIs to demonstrate 0-RTT.
// This test does not use the project's QUIC code. It directly wires SSL_quic_method
// callbacks and shuttles handshake/post-handshake messages between peers.

namespace quicx {
namespace quic {
namespace {

static const char kCertPem[] =
      "-----BEGIN CERTIFICATE-----\n"
      "MIICWDCCAcGgAwIBAgIJAPuwTC6rEJsMMA0GCSqGSIb3DQEBBQUAMEUxCzAJBgNV\n"
      "BAYTAkFVMRMwEQYDVQQIDApTb21tLVN0YXRlMSEwHwYDVQQKDBhJbnRlcm5ldCBX\n"
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

static const char kKeyPem[] =
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

// Simple buffer to accumulate outgoing handshake data per encryption level.
struct OutRecord {
    ssl_encryption_level_t level;
    std::vector<uint8_t> data;
};

struct QuicPeer {
    SSL* ssl = nullptr;
    std::unordered_map<ssl_encryption_level_t, std::vector<uint8_t>> out;
    bool saw_early_write_secret = false;
    bool saw_early_read_secret = false;
};

static SSL_SESSION* g_captured_session = nullptr;
static int OnNewSession(SSL* /*ssl*/, SSL_SESSION* sess) {
    if (g_captured_session) {
        SSL_SESSION_free(g_captured_session);
        g_captured_session = nullptr;
    }
    g_captured_session = sess;
    SSL_SESSION_up_ref(g_captured_session);
    return 0; // keep ownership in lib
}

static int OnSetReadSecret(SSL* ssl, ssl_encryption_level_t level, const SSL_CIPHER* /*cipher*/, const uint8_t* /*secret*/, size_t /*secret_len*/) {
    QuicPeer* self = reinterpret_cast<QuicPeer*>(SSL_get_app_data(ssl));
    if (!self) return 0;
    if (level == ssl_encryption_early_data) self->saw_early_read_secret = true;
    return 1;
}

static int OnSetWriteSecret(SSL* ssl, ssl_encryption_level_t level, const SSL_CIPHER* /*cipher*/, const uint8_t* /*secret*/, size_t /*secret_len*/) {
    QuicPeer* self = reinterpret_cast<QuicPeer*>(SSL_get_app_data(ssl));
    if (!self) return 0;
    if (level == ssl_encryption_early_data) self->saw_early_write_secret = true;
    return 1;
}

static int OnAddHandshakeData(SSL* ssl, ssl_encryption_level_t level, const uint8_t* data, size_t len) {
    QuicPeer* self = reinterpret_cast<QuicPeer*>(SSL_get_app_data(ssl));
    if (!self) return 0;
    self->out[level].assign(data, data + len);
    return 1;
}

static int OnFlushFlight(SSL* /*ssl*/) {
    return 1;
}

static int OnSendAlert(SSL* /*ssl*/, ssl_encryption_level_t /*level*/, uint8_t /*alert*/) {
    return 1;
}

static const SSL_QUIC_METHOD kQuicMethod = {
    OnSetReadSecret,
    OnSetWriteSecret,
    OnAddHandshakeData,
    OnFlushFlight,
    OnSendAlert,
};

static int AlpnSelectCb(SSL* /*ssl*/, const unsigned char** out, unsigned char* outlen,
                        const unsigned char* in, unsigned int inlen, void* /*arg*/) {
    const char* proto = "h3";
    for (unsigned int i = 0; i < inlen;) {
        unsigned int len = in[i++];
        if (i + len <= inlen && len == 2 && memcmp(in + i, proto, 2) == 0) {
            *out = reinterpret_cast<const unsigned char*>(proto);
            *outlen = 2;
            return SSL_TLSEXT_ERR_OK;
        }
        i += len;
    }
    return SSL_TLSEXT_ERR_NOACK;
}

static void SetClientALPN(SSL* ssl) {
    const uint8_t alpn[] = {2, 'h', '3'}; // length-prefixed list
    SSL_set_alpn_protos(ssl, alpn, sizeof(alpn));
}

static void SetServerALPN(SSL_CTX* ctx) {
    SSL_CTX_set_alpn_select_cb(ctx, AlpnSelectCb, nullptr);
}

static bool LoadCertKeyToCtx(SSL_CTX* ctx, const char* cert_pem, const char* key_pem) {
    BIO* cert_bio = BIO_new_mem_buf(cert_pem, static_cast<int>(strlen(cert_pem)));
    if (!cert_bio) return false;
    X509* cert = PEM_read_bio_X509(cert_bio, nullptr, nullptr, nullptr);
    BIO_free(cert_bio);
    if (!cert) return false;
    bool ok = SSL_CTX_use_certificate(ctx, cert) == 1;
    X509_free(cert);
    if (!ok) return false;

    BIO* key_bio = BIO_new_mem_buf(key_pem, static_cast<int>(strlen(key_pem)));
    if (!key_bio) return false;
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr);
    BIO_free(key_bio);
    if (!pkey) return false;
    ok = SSL_CTX_use_PrivateKey(ctx, pkey) == 1;
    EVP_PKEY_free(pkey);
    if (!ok) return false;
    return SSL_CTX_check_private_key(ctx) == 1;
}

static void SetQuicTransportParams(SSL* ssl) {
    static const uint8_t kDummyTp[] = {0x00};
    SSL_set_quic_transport_params(ssl, kDummyTp, sizeof(kDummyTp));
}

// Shuttle only records at receiver's current read level; keep others for later.
static void Transfer(QuicPeer& a, QuicPeer& b) {
    bool progressed = true;
    while (progressed) {
        progressed = false;
        auto want = SSL_quic_read_level(b.ssl);
        for (auto it = a.out.begin(); it != a.out.end();) {
            if (it->first != want) { ++it; continue; }
            int ok = SSL_provide_quic_data(b.ssl, it->first, it->second.data(), it->second.size());
            ASSERT_EQ(1, ok);
            if (it->first == ssl_encryption_application) {
                SSL_process_quic_post_handshake(b.ssl);
            }
            a.out.erase(it++);
            progressed = true;
        }
    }
}

static void DriveHandshake(QuicPeer& client, QuicPeer& server) {
    for (int iter = 0; iter < 2000; ++iter) {
        int rc_c = SSL_do_handshake(client.ssl);
        if (rc_c <= 0) {
            (void)SSL_get_error(client.ssl, rc_c);
        }
        Transfer(client, server);

        int rc_s = SSL_do_handshake(server.ssl);
        if (rc_s <= 0) {
            (void)SSL_get_error(server.ssl, rc_s);
        }
        Transfer(server, client);

        if (SSL_is_init_finished(client.ssl) && SSL_is_init_finished(server.ssl)) {
            break;
        }
    }
    ASSERT_TRUE(SSL_is_init_finished(client.ssl));
    ASSERT_TRUE(SSL_is_init_finished(server.ssl));
}


TEST(bssl_quic_0rtt_demo, early_data_resume_basic) {
    SSL_CTX* server_ctx = SSL_CTX_new(TLS_method());
    ASSERT_NE(server_ctx, nullptr);
    SSL_CTX_set_min_proto_version(server_ctx, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(server_ctx, TLS1_3_VERSION);
    ASSERT_TRUE(LoadCertKeyToCtx(server_ctx, kCertPem, kKeyPem));
    SSL_CTX_set_early_data_enabled(server_ctx, 1);
    ASSERT_EQ(1, SSL_CTX_set_quic_method(server_ctx, &kQuicMethod));
    SetServerALPN(server_ctx);

    SSL_CTX* client_ctx = SSL_CTX_new(TLS_method());
    ASSERT_NE(client_ctx, nullptr);
    SSL_CTX_set_min_proto_version(client_ctx, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(client_ctx, TLS1_3_VERSION);
    SSL_CTX_sess_set_new_cb(client_ctx, OnNewSession);
    SSL_CTX_set_session_cache_mode(client_ctx, SSL_SESS_CACHE_BOTH);
    SSL_CTX_set_early_data_enabled(client_ctx, 1);
    ASSERT_EQ(1, SSL_CTX_set_quic_method(client_ctx, &kQuicMethod));

    // First handshake to obtain a ticket with early-data capability
    QuicPeer cli1{};
    QuicPeer srv1{};

    cli1.ssl = SSL_new(client_ctx);
    srv1.ssl = SSL_new(server_ctx);
    ASSERT_NE(cli1.ssl, nullptr);
    ASSERT_NE(srv1.ssl, nullptr);

    SSL_set_app_data(cli1.ssl, &cli1);
    SSL_set_app_data(srv1.ssl, &srv1);
    ASSERT_EQ(1, SSL_set_quic_method(cli1.ssl, &kQuicMethod));
    ASSERT_EQ(1, SSL_set_quic_method(srv1.ssl, &kQuicMethod));

    SSL_set_accept_state(srv1.ssl);
    SSL_set_connect_state(cli1.ssl);

    static const char kEarlyCtx[] = "quic-early-data";
    ASSERT_EQ(1, SSL_set_quic_early_data_context(srv1.ssl, reinterpret_cast<const uint8_t*>(kEarlyCtx), sizeof(kEarlyCtx) - 1));
    ASSERT_EQ(1, SSL_set_quic_early_data_context(cli1.ssl, reinterpret_cast<const uint8_t*>(kEarlyCtx), sizeof(kEarlyCtx) - 1));

    SetClientALPN(cli1.ssl);

    
    SetQuicTransportParams(cli1.ssl);
    SetQuicTransportParams(srv1.ssl);

    DriveHandshake(cli1, srv1);

    // Process post-handshake messages to capture session ticket
    for (int i = 0; i < 64; ++i) {
        (void)SSL_do_handshake(srv1.ssl);
        Transfer(srv1, cli1);
        SSL_process_quic_post_handshake(cli1.ssl);
        if (g_captured_session) break;
    }

    ASSERT_NE(g_captured_session, nullptr);

    SSL_free(cli1.ssl);
    SSL_free(srv1.ssl);

    // Second handshake with 0-RTT
    QuicPeer cli2{};
    QuicPeer srv2{};
    cli2.ssl = SSL_new(client_ctx);
    srv2.ssl = SSL_new(server_ctx);
    ASSERT_NE(cli2.ssl, nullptr);
    ASSERT_NE(srv2.ssl, nullptr);

    SSL_set_app_data(cli2.ssl, &cli2);
    SSL_set_app_data(srv2.ssl, &srv2);
    ASSERT_EQ(1, SSL_set_quic_method(cli2.ssl, &kQuicMethod));
    ASSERT_EQ(1, SSL_set_quic_method(srv2.ssl, &kQuicMethod));

    ASSERT_EQ(1, SSL_set_quic_early_data_context(srv2.ssl, reinterpret_cast<const uint8_t*>(kEarlyCtx), sizeof(kEarlyCtx) - 1));
    ASSERT_EQ(1, SSL_set_quic_early_data_context(cli2.ssl, reinterpret_cast<const uint8_t*>(kEarlyCtx), sizeof(kEarlyCtx) - 1));

    SSL_set_early_data_enabled(cli2.ssl, 1);
    ASSERT_EQ(1, SSL_set_session(cli2.ssl, g_captured_session));

    SetClientALPN(cli2.ssl);
    SetQuicTransportParams(cli2.ssl);
    SetQuicTransportParams(srv2.ssl);

    SSL_set_accept_state(srv2.ssl);
    SSL_set_connect_state(cli2.ssl);

    // Start client handshake - should return immediately for 0-RTT
    int rc = SSL_do_handshake(cli2.ssl);
    if (rc <= 0) {
        (void)SSL_get_error(cli2.ssl, rc);
    }
    Transfer(cli2, srv2);
    
    // Start server handshake
    rc = SSL_do_handshake(srv2.ssl);
    if (rc <= 0) {
        (void)SSL_get_error(srv2.ssl, rc);
    }
    Transfer(srv2, cli2);

    // Verify 0-RTT state
    EXPECT_TRUE(SSL_in_early_data(cli2.ssl));
    EXPECT_TRUE(cli2.saw_early_write_secret);
    
    // Complete the handshake
    DriveHandshake(cli2, srv2);

    // Verify 0-RTT was accepted
    EXPECT_TRUE(cli2.saw_early_write_secret);
    EXPECT_TRUE(SSL_early_data_accepted(cli2.ssl));
    EXPECT_TRUE(SSL_session_reused(cli2.ssl));
    EXPECT_TRUE(SSL_session_reused(srv2.ssl));

    SSL_SESSION_free(g_captured_session);
    g_captured_session = nullptr;

    SSL_free(cli2.ssl);
    SSL_free(srv2.ssl);
    SSL_CTX_free(client_ctx);
    SSL_CTX_free(server_ctx);
}

}
}
}


