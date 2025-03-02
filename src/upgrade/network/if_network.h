#ifndef UPGRADE_NETWORK_IF_NETWORK
#define UPGRADE_NETWORK_IF_NETWORK

#include <string>
#include <memory>
#include <cstdint>
#include "upgrade/upgrade/if_socket_handler.h"

namespace quicx {
namespace upgrade {

class IAction {
public:
    IAction() {}
    virtual ~IAction() {}

    virtual void Listen(const std::string& addr, uint16_t port) = 0;
    virtual void SetSocketHandler(std::shared_ptr<IfSocketHandler> handler) = 0;
};

}
}

#endif
