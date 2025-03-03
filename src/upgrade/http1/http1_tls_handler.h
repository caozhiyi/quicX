#ifndef UPGRADE_HTTP1_HTTP1_TLS_HANDLER
#define UPGRADE_HTTP1_HTTP1_TLS_HANDLER

#include "upgrade/upgrade/crypto_handler.h"

namespace quicx {
namespace upgrade {

class Http1TlsHandler:
    public CryptoHandler {
public:
    Http1TlsHandler() {}
    virtual ~Http1TlsHandler() {}

    virtual void HandleSocketData(std::shared_ptr<ISocket> socket) override;
};

}
}


#endif