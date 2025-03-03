#ifndef UPGRADE_UPGRADE_PLAIN_HANDLER
#define UPGRADE_UPGRADE_PLAIN_HANDLER

#include <functional>
#include <unordered_map>
#include "upgrade/upgrade/type.h"
#include "upgrade/upgrade/if_socket_handler.h"

namespace quicx {
namespace upgrade {

class PlainHandler:
    public IfSocketHandler {
public:
    PlainHandler() {}
    virtual ~PlainHandler() {}

    void RegisterHandler(HttpVersion http_version, std::shared_ptr<IfSocketHandler> handler);

    virtual void HandleSocketConnect(uint64_t listen_socket) override;
    virtual void HandleSocketData(std::shared_ptr<ISocket> socket) = 0;
    virtual void HandleSocketClose(std::shared_ptr<ISocket> socket) override;

    virtual void ReadData(std::shared_ptr<ISocket> socket) override;
    virtual void WriteData(std::shared_ptr<ISocket> socket) override;
};

}
}

#endif