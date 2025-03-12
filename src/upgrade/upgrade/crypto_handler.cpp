#include "openssl/err.h"
#include "common/log/log.h"
#include "common/network/io_handle.h"
#include "upgrade/network/tcp_socket.h"
#include "upgrade/network/if_tcp_action.h"
#include "upgrade/upgrade/crypto_handler.h"

namespace quicx {
namespace upgrade {

CryptoHandler::CryptoHandler(char* cert_pem, char* key_pem) {
    ctx_ = SSL_CTX_new(TLS_server_method());
    if (!ctx_) {
        common::LOG_ERROR("Failed to create SSL context");
        return;
    }

    // Set certificate and private key
    if (SSL_CTX_use_certificate_file(ctx_, cert_pem, SSL_FILETYPE_PEM) <= 0) {
        common::LOG_ERROR("Failed to load certificate");
        SSL_CTX_free(ctx_);
        return;
    }

    if (SSL_CTX_use_PrivateKey_file(ctx_, key_pem, SSL_FILETYPE_PEM) <= 0) {
        common::LOG_ERROR("Failed to load private key");
        SSL_CTX_free(ctx_);
        return;
    }
}

CryptoHandler::CryptoHandler(const std::string& cert_file, const std::string& key_file) {
    ctx_ = SSL_CTX_new(TLS_server_method());
    if (!ctx_) {
        common::LOG_ERROR("Failed to create SSL context");
        return;
    }

    // Set certificate and private key
    if (SSL_CTX_use_certificate_file(ctx_, cert_file.c_str(), SSL_FILETYPE_PEM) <= 0) {
        common::LOG_ERROR("Failed to load certificate");
        SSL_CTX_free(ctx_);
        return;
    }

    if (SSL_CTX_use_PrivateKey_file(ctx_, key_file.c_str(), SSL_FILETYPE_PEM) <= 0) {
        common::LOG_ERROR("Failed to load private key");
        SSL_CTX_free(ctx_);
        return;
    }
}

CryptoHandler::~CryptoHandler() {
    SSL_CTX_free(ctx_);
}

void CryptoHandler::HandleConnect(std::shared_ptr<TcpSocket> socket, std::shared_ptr<ITcpAction> action) {
    uint64_t sock = socket->GetSocket();

    // Accept incoming connections and perform SSL handshake
    while (true) {
        common::Address addr;
        auto ret = common::Accept(sock, addr);
        if (ret.return_value_ == -1) {
            if (ret.errno_ != 0) {
                common::LOG_ERROR("accept socket failed. error:%d", ret.errno_);
            }
            break;
        }

        // set socket no blocking
        auto no_block_ret = common::SocketNoblocking(ret.return_value_);
        if (no_block_ret.return_value_ == -1) {
            common::LOG_ERROR("set socket no blocking failed. error:%d", no_block_ret.errno_);
            common::Close(ret.return_value_);
            continue;
        }

        // Create new SSL structure
        SSL* ssl = SSL_new(ctx_);
        if (!ssl) {
            common::LOG_ERROR("Failed to create SSL structure");
            SSL_CTX_free(ctx_);
            common::Close(ret.return_value_);
            continue;
        }

        // Bind socket to SSL
        SSL_set_fd(ssl, ret.return_value_);

        CryptoContext* crypto_context = new CryptoContext(ssl);
        socket->SetContext(crypto_context);

        MaybeSSLHandshake(socket);

        // Create new socket with SSL
        auto new_socket = std::make_shared<TcpSocket>(ret.return_value_, addr, action, shared_from_this(), pool_block_);
        sockets_[ret.return_value_] = new_socket;
        common::LOG_DEBUG("accept a new SSL socket. socket: %d", ret.return_value_);
        action->AddReceiver(new_socket);
    }
}

void CryptoHandler::HandleRead(std::shared_ptr<TcpSocket> socket) {
    MaybeSSLHandshake(socket);

    // nullptr check is done in MaybeSSLHandshake
    CryptoContext* crypto_context = (CryptoContext*)socket->GetContext();
    SSL* ssl = crypto_context->GetSSL();

    auto read_buffer = socket->GetReadBuffer();
    if (!read_buffer) {
        common::LOG_ERROR("read buffer is nullptr");
        HandleClose(socket);
        return;
    }

    // Read encrypted data from SSL connection
    while (true) {
        auto block = read_buffer->GetWriteBuffers(1024);
        int bytes_read = SSL_read(ssl, block->GetData(), block->GetFreeLength());
        if (bytes_read > 0) {
            block->MoveWritePt(bytes_read);
            common::LOG_DEBUG("recv data from socket: %d, size: %d", socket->GetSocket(), bytes_read);

            // if read data is less than block size, break
            if (bytes_read < block->GetFreeLength()) {
                break;
            }

        } else {
            int err = SSL_get_error(ssl, bytes_read);
            if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
                common::LOG_ERROR("recv data failed. error:%d", err);
                HandleClose(socket);
            }
        }
    }

    DispatchHttpHandler(socket);

    // process send data
    HandleWrite(socket);
}

void CryptoHandler::HandleWrite(std::shared_ptr<TcpSocket> socket) {
    common::LOG_DEBUG("handle write");
    auto write_buffer = socket->GetWriteBuffer();
    if (!write_buffer) {
        common::LOG_ERROR("write buffer is nullptr");
        HandleClose(socket);
        return;
    }

    // if write buffer is empty, return
    if (write_buffer->GetDataLength() == 0) {
        return;
    }

    common::LOG_DEBUG("handle real write. size: %d", write_buffer->GetDataLength());

    CryptoContext* crypto_context = (CryptoContext*)socket->GetContext();
    if (!crypto_context) {
        common::LOG_ERROR("crypto context is nullptr");
        HandleClose(socket);
        return;
    }

    SSL* ssl = crypto_context->GetSSL();
    if (!ssl) {
        common::LOG_ERROR("ssl is nullptr");
        HandleClose(socket);
        return;
    }

    auto action = socket->GetAction();
    if (!action) {
        common::LOG_ERROR("action is nullptr");
        HandleClose(socket);
        return;
    }

    // Write data to SSL connection
    while (write_buffer->GetDataLength() > 0) {
        auto block = write_buffer->GetReadBuffers();
        int bytes_written = SSL_write(ssl, block->GetData(), block->GetDataLength());

        if (bytes_written <= 0) {
            int err = SSL_get_error(ssl, bytes_written);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                // Need to wait for more buffer space
                action->AddSender(socket);
                return;
            }
            // Handle other SSL errors
            common::LOG_ERROR("SSL_write failed with error: %d", err);
            HandleClose(socket);
            return;
        }

        block->MoveReadPt(bytes_written);
        common::LOG_DEBUG("sent encrypted data to socket: %d, size: %d", socket->GetSocket(), bytes_written);

        // If we couldn't write all the data, we'll need to try again later
        if (bytes_written < block->GetDataLength()) {
            action->AddSender(socket);
            break;
        }
    }
}

void CryptoHandler::HandleClose(std::shared_ptr<TcpSocket> socket) {
    common::Close(socket->GetSocket());
    sockets_.erase(socket->GetSocket());
    auto context = (CryptoContext*)socket->GetContext();
    if (context) {
        delete context;
    }
}

void CryptoHandler::MaybeSSLHandshake(std::shared_ptr<TcpSocket> socket) {
    CryptoContext* crypto_context = (CryptoContext*)socket->GetContext();
    if (!crypto_context) {
        common::LOG_ERROR("crypto context is nullptr");
        HandleClose(socket);
        return;
    }

    // already done, no need to do anything
    if (crypto_context->IsSSLHandshakeDone()) {
        return;
    }

    SSL* ssl = crypto_context->GetSSL();
    if (!ssl) {
        common::LOG_ERROR("ssl is nullptr");
        HandleClose(socket);
        return;
    }

    // try to continue handshake
    int ret = SSL_accept(ssl);
    if (ret <= 0) {
        int err = SSL_get_error(ssl, ret);
        if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
            // Handle other errors
            common::LOG_ERROR("SSL handshake failed");
            SSL_free(ssl);
            HandleClose(socket);
            return;
        }
    }

    // SSL handshake completed successfully
    common::LOG_DEBUG("SSL handshake completed");
    crypto_context->SetSSLHandshakeDone(true);
}

}
}