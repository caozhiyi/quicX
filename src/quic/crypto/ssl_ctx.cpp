#include "openssl/conf.h"

#include "common/log/log.h"
#include "quic/crypto/ssl_ctx.h"

namespace quicx {

SSLCtx::SSLCtx():
    _ssl_ctx(nullptr),
    _ssl_connection_index(-1),
    _ssl_server_conf_index(-1),
    _ssl_session_cache_index(-1),
    _ssl_session_ticket_keys_index(-1),
    _ssl_ocsp_index(-1),
    _ssl_certificate_index(-1),
    _ssl_next_certificate_index(-1),
    _ssl_certificate_name_index(-1),
    _ssl_stapling_index(-1) {

}

SSLCtx::~SSLCtx() {

}

bool SSLCtx::Init() {
    _ssl_ctx = SSL_CTX_new(SSLv23_method());
    if (_ssl_ctx == nullptr) {
        LOG_ERROR("create ssl ctx failed");
        return false;
    }
    
    OPENSSL_config(nullptr);
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    _ssl_connection_index = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    if (_ssl_connection_index == -1) {
        LOG_ERROR("SSL_get_ex_new_index failed");
        return false;
    }

    _ssl_server_conf_index = SSL_CTX_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    if (_ssl_server_conf_index == -1) {
        LOG_ERROR("SSL_CTX_get_ex_new_index failed");
        return false;
    }

    _ssl_session_cache_index = SSL_CTX_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    if (_ssl_session_cache_index == -1) {
        LOG_ERROR("SSL_CTX_get_ex_new_index failed");
        return false;
    }

    _ssl_session_ticket_keys_index = SSL_CTX_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    if (_ssl_session_ticket_keys_index == -1) {
        LOG_ERROR("SSL_CTX_get_ex_new_index failed");
        return false;
    }

    _ssl_ocsp_index = SSL_CTX_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    if (_ssl_ocsp_index == -1) {
        LOG_ERROR("SSL_CTX_get_ex_new_index failed");
        return false;
    }

    _ssl_certificate_index = SSL_CTX_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    if (_ssl_certificate_index == -1) {
        LOG_ERROR("SSL_CTX_get_ex_new_index failed");
        return false;
    }

    _ssl_next_certificate_index = X509_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    if (_ssl_next_certificate_index == -1) {
        LOG_ERROR("X509_get_ex_new_index failed");
        return false;
    }

    _ssl_certificate_name_index = X509_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    if (_ssl_certificate_name_index == -1) {
        LOG_ERROR("X509_get_ex_new_index failed");
        return false;
    }

    _ssl_stapling_index = X509_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    if (_ssl_stapling_index == -1) {
        LOG_ERROR("X509_get_ex_new_index failed");
        return false;
    }

    return true;
}

bool SSLCtx::SetCiphers(const std::string& ciphers, bool prefer_server_ciphers) {
    if (!_ssl_ctx) {
        LOG_ERROR("ssl ctx is not ready.");
        return false;
    }
    
    if (SSL_CTX_set_cipher_list(_ssl_ctx, ciphers.c_str()) == 0) {
        LOG_ERROR("SSL_CTX_set_cipher_list failed, ciphers:%s", ciphers.c_str());
        return false;
    }

    if (prefer_server_ciphers) {
        SSL_CTX_set_options(_ssl_ctx, SSL_OP_CIPHER_SERVER_PREFERENCE);
    }

    return true;
}

bool SSLCtx::SetCertificateAndKey(const std::string& cert_path, const std::string& key_path, const std::string& key_pwd) {
    if (!_ssl_ctx) {
        LOG_ERROR("ssl ctx is not ready.");
        return false;
    }

    // install certificate
    X509* x509 = LoadCertificate(cert_path);
    if (!x509) {
        LOG_ERROR("load certificate failed.");
        return false;
    }
    if (SSL_CTX_use_certificate(_ssl_ctx, x509) == 0) {
        LOG_ERROR("SSL_CTX_use_certificate failed");
        X509_free(x509);
        return false;
    }

    // install private keys
    EVP_PKEY* key = LoadCertificateKey(key_path, key_pwd);
    if (!key) {
        LOG_ERROR("load certificate key failed");
        X509_free(x509);
        return false;
    }

    if (SSL_CTX_use_PrivateKey(_ssl_ctx, key) == 0) {
        LOG_ERROR("SSL_CTX_use_PrivateKey failed");
        EVP_PKEY_free(key);
        return false;
    }

    EVP_PKEY_free(key);
    return true;
}

X509* SSLCtx::LoadCertificate(const std::string& cert_path) {
    BIO *bio = BIO_new_file(cert_path.c_str(), "r");
    if (bio == nullptr) {
        LOG_ERROR("BIO_new_file failed");
        return nullptr;
    }

    X509 *x509 = PEM_read_bio_X509_AUX(bio, nullptr, nullptr, nullptr);
    if (x509 == nullptr) {
        LOG_ERROR("PEM_read_bio_X509_AUX failed");
        BIO_free(bio);
        return nullptr;
    }

    BIO_free(bio);
    return x509;
}

EVP_PKEY* SSLCtx::LoadCertificateKey(const std::string& key_path, const std::string& key_pwd) {
    BIO *bio = BIO_new_file(key_path.c_str(), "r");
    if (bio == NULL) {
        LOG_ERROR("BIO_new_file failed");
        return nullptr;
    }

    // todo support password
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    if (pkey == NULL) {
        LOG_ERROR("PEM_read_bio_PrivateKey failed");
        BIO_free(bio);
        return nullptr;
    }

    BIO_free(bio);
    return pkey;
}

}