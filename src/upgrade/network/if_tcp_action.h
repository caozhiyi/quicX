#ifndef UPGRADE_NETWORK_IF_TCP_ACTION
#define UPGRADE_NETWORK_IF_TCP_ACTION

#include <memory>
#include <cstdint>

namespace quicx {
namespace upgrade {

class TcpSocket;
class ITcpAction:
    public std::enable_shared_from_this<ITcpAction> {
public:
    ITcpAction() {}
    virtual ~ITcpAction() {}

    virtual bool AddListener(std::shared_ptr<TcpSocket> socket) = 0;
    virtual bool AddReceiver(std::shared_ptr<TcpSocket> socket) = 0;
    virtual bool AddSender(std::shared_ptr<TcpSocket> socket) = 0;
    virtual void Remove(std::shared_ptr<TcpSocket> socket) = 0;

    virtual void Wait(uint32_t timeout_ms) = 0;
    virtual void Wakeup() = 0;
};

}
}

#endif
