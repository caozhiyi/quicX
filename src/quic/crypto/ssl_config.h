#ifndef QUIC_CRYPTO_SSL_CONFIG
#define QUIC_CRYPTO_SSL_CONFIG

#include "openssl/ssl.h"
#include "common/util/singleton.h"

namespace quicx {

class SSLConfig:
    public Singleton<SSLConfig> {
    bool Init()

private:
    SSL_CTX *_ssl_ctx;

    int32_t _ssl_connection_index;
    int32_t _ssl_server_conf_index;
    int32_t _ssl_session_cache_index;
    int32_t _ssl_session_ticket_keys_index;
    int32_t _ssl_ssl_ocsp_index;
    int32_t _ssl_certificate_index;
    int32_t _ssl_next_certificate_index;
    int32_t _ssl_certificate_name_index;
    int32_t _ssl_stapling_index;
};


}

#endif