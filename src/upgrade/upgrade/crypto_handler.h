#ifndef UPGRADE_UPGRADE_CRYPTO_HANDLER
#define UPGRADE_UPGRADE_CRYPTO_HANDLER

#include <functional>
#include <unordered_map>

#include "openssl/ssl.h"
#include "upgrade/upgrade/type.h"
#include "common/util/c_smart_ptr.h"
#include "upgrade/upgrade/if_socket_handler.h"

namespace quicx {
namespace upgrade {

class CryptoContext {
public:
    CryptoContext(SSL* ssl):
        ssl_(ssl),
        is_ssl_handshake_done_(false) {
    }

    ~CryptoContext() {
        SSL_free(ssl_);
    }

    SSL* GetSSL() { return ssl_; }

    bool IsSSLHandshakeDone() { return is_ssl_handshake_done_; }
    void SetSSLHandshakeDone(bool is_ssl_handshake_done) { is_ssl_handshake_done_ = is_ssl_handshake_done; }

private:
    SSL* ssl_ = nullptr;
    bool is_ssl_handshake_done_ = false;
};

class CryptoHandler:
    public ISocketHandler {
public:
    CryptoHandler(char* cert_pem, char* key_pem);
    CryptoHandler(const std::string& cert_file, const std::string& key_file);
    virtual ~CryptoHandler();

    virtual void HandleConnect(std::shared_ptr<TcpSocket> socket, std::shared_ptr<ITcpAction> action) override;
    virtual void HandleRead(std::shared_ptr<TcpSocket> socket) override;
    virtual void HandleWrite(std::shared_ptr<TcpSocket> socket) override;
    virtual void HandleClose(std::shared_ptr<TcpSocket> socket) override;

private:
    void MaybeSSLHandshake(std::shared_ptr<TcpSocket> socket);

private:
    SSL_CTX* ctx_;
};

}
}


#endif