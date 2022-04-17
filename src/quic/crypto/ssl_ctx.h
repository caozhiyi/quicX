#ifndef QUIC_CRYPTO_SSL_CTX
#define QUIC_CRYPTO_SSL_CTX

#include <string>
#include <cstdint>
#include "openssl/ssl.h"
#include "common/util/singleton.h"

namespace quicx {

class SSLCtx:
    public Singleton<SSLCtx> {
public:
    SSLCtx();
    ~SSLCtx();
    // init ssl library and create global ssl ctx
    bool Init();
    // set ciphers to ssl
    bool SetCiphers(const std::string& ciphers, bool prefer_server_ciphers);
    // set certificate and key
    bool SetCertificateAndKey(const std::string& cert_path, const std::string& key_path, const std::string& key_pwd);
    // get ssl ctx
    SSL_CTX* GetSSLCtx() { return _ssl_ctx; }
    // get connection index
    int32_t GetSslConnectionIndex() { return _ssl_connection_index; }

private:
    X509* LoadCertificate(const std::string& cert_path);
    EVP_PKEY* LoadCertificateKey(const std::string& key_path, const std::string& key_pwd);

private:
    SSL_CTX *_ssl_ctx;

    int32_t _ssl_connection_index;
    int32_t _ssl_server_conf_index;
    int32_t _ssl_session_cache_index;
    int32_t _ssl_session_ticket_keys_index;
    int32_t _ssl_ocsp_index;
    int32_t _ssl_certificate_index;
    int32_t _ssl_next_certificate_index;
    int32_t _ssl_certificate_name_index;
    int32_t _ssl_stapling_index;
};


}

#endif