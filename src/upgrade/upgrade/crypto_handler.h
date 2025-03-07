#ifndef UPGRADE_UPGRADE_CRYPTO_HANDLER
#define UPGRADE_UPGRADE_CRYPTO_HANDLER

#include <functional>
#include <unordered_map>
#include "upgrade/upgrade/type.h"
#include "upgrade/upgrade/if_socket_handler.h"

namespace quicx {
namespace upgrade {

class CryptoHandler:
    public ISocketHandler {
public:
    CryptoHandler() {}
    virtual ~CryptoHandler() {}

    virtual void HandleConnect(std::shared_ptr<ISocket> socket, ITcpAction* action) override;
    virtual void HandleRead(std::shared_ptr<ISocket> socket) override;
    virtual void HandleWrite(std::shared_ptr<ISocket> socket) override;
    virtual void HandleClose(std::shared_ptr<ISocket> socket) override;

    virtual void ReadData(std::shared_ptr<ISocket> socket) override;
    virtual void WriteData(std::shared_ptr<ISocket> socket) override;
};

}
}


#endif