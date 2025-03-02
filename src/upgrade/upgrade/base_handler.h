#ifndef UPGRADE_UPGRADE_BASE_HANDLER
#define UPGRADE_UPGRADE_BASE_HANDLER

#include <functional>
#include <unordered_map>
#include "upgrade/upgrade/if_socket_handler.h"

namespace quicx {
namespace upgrade {

enum class HandlerType {
    kHttp1Plain,
    kHttp1Tls,
    kHttp2Plain,
    kHttp2Tls,
};

class BaseHandler:
    public IfSocketHandler {
public:
    BaseHandler() {}
    virtual ~BaseHandler() {}

    void SetTlsChecker(std::function<bool(common::Address&)> checker);

    virtual void RegisterHandler(HandlerType type, std::shared_ptr<IfSocketHandler> handler);

    virtual void HandleSocketConnect(std::shared_ptr<ISocket> socket) override;
    virtual void HandleSocketData(std::shared_ptr<ISocket> socket, std::shared_ptr<common::IBufferRead> buffer) override;

private:
    std::function<bool(common::Address&)> tls_checker_;
    std::unordered_map<HandlerType, std::shared_ptr<IfSocketHandler>> handlers_;
};

}
}


#endif