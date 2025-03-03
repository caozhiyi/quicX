#ifndef UPGRADE_HTTP2_HTTP2_TLS_HANDLER
#define UPGRADE_HTTP2_HTTP2_TLS_HANDLER

#include "upgrade/upgrade/crypto_handler.h"

namespace quicx {
namespace upgrade {

class Http2TlsHandler:
    public CryptoHandler {
public:
    Http2TlsHandler() {}
    virtual ~Http2TlsHandler() {}

    virtual void HandleSocketData(std::shared_ptr<ISocket> socket) override;
};

}
}


#endif