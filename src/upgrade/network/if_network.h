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

    virtual void AddListener(std::shared_ptr<ISocket> socket) = 0;
    virtual void AddReceiver(std::shared_ptr<ISocket> socket) = 0;
    virtual void AddSender(std::shared_ptr<ISocket> socket) = 0;
    virtual void Remove(std::shared_ptr<ISocket> socket) = 0;

    virtual void Wait(uint32_t timeout_ms) = 0;
    virtual void Wakeup() = 0;
};

}
}

#endif
