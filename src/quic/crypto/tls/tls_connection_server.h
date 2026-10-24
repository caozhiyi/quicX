#ifndef QUIC_CRYPTO_TLS_TLS_SERVER_CONNECTION
#define QUIC_CRYPTO_TLS_TLS_SERVER_CONNECTION

#include <memory>
#include <vector>

#include "quic/crypto/tls/tls_connection.h"

namespace quicx {
namespace quic {

class TlsServerHandlerInterface {
public:
    TlsServerHandlerInterface() {}
    virtual ~TlsServerHandlerInterface() {}

    virtual void SSLAlpnSelect(
        const unsigned char** out, unsigned char* outlen, const unsigned char* in, unsigned int inlen, void* arg) = 0;
};

class TLSServerConnection: public TLSConnection {
public:
    TLSServerConnection(
        std::shared_ptr<TLSCtx> ctx, TlsHandlerInterface* handler, TlsServerHandlerInterface* ser_handler);
    ~TLSServerConnection();
    // init ssl connection
    virtual bool Init();

    // do handshake
    virtual bool DoHandleShake();

    // 0-RTT anti-replay context: compared by the TLS stack against the one
    // baked into the resumption ticket, so tickets minted under a different
    // configuration are rejected for early data. Must be set before the
    // handshake processes the ClientHello; a constant literal is used if not.
    void SetEarlyDataContext(const uint8_t* ctx, size_t len) { early_data_ctx_.assign(ctx, ctx + len); }

    // add transport param
    virtual bool AddTransportParam(uint8_t* tp, uint32_t len);

private:
    static int SSLAlpnSelect(SSL* ssl, const unsigned char** out, unsigned char* outlen, const unsigned char* in,
        unsigned int inlen, void* arg);

private:
    TlsServerHandlerInterface* ser_handler_;
    // P0-S9: caller-supplied early-data context (see SetEarlyDataContext).
    std::vector<uint8_t> early_data_ctx_;
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CRYPTO_TLS_TLS_SERVER_CONNECTION