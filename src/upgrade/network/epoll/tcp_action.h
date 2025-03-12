#ifndef UPGRADE_NETWORK_EPOLL_TCP_ACTION
#define UPGRADE_NETWORK_EPOLL_TCP_ACTION

#ifdef __linux__

#include <sys/epoll.h>
#include <unordered_map>
#include "upgrade/network/if_tcp_action.h"

namespace quicx {
namespace upgrade {

class TcpAction:
    public ITcpAction {
public:
    TcpAction();
    virtual ~TcpAction();

    virtual void AddReceiver(std::shared_ptr<ISocket> socket) override;
    virtual void AddSender(std::shared_ptr<ISocket> socket) override;
    virtual void Remove(std::shared_ptr<ISocket> socket) override;

    virtual void Wait(uint32_t timeout_ms) override;
    virtual void Wakeup() override;
    
private:
    uint32_t pipe_[2];
    int32_t epoll_handler_;

    epoll_event pipe_content_;
    std::vector<epoll_event> active_list_;
    std::unordered_map<uint64_t, std::weak_ptr<ISocket>> socket_map_;
};

}
}

#endif // __linux__
#endif