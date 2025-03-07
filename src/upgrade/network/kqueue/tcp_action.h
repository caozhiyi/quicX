#ifndef UPGRADE_NETWORK_KQUEUE_TCP_ACTION
#define UPGRADE_NETWORK_KQUEUE_TCP_ACTION

#ifdef __APPLE__

#include <memory>
#include <sys/types.h>
#include <sys/event.h>
#include <unordered_set>
#include <unordered_map>
#include "upgrade/network/if_tcp_action.h"

namespace quicx {
namespace upgrade {

class TcpAction:
    public ITcpAction {
public:
    TcpAction();
    virtual ~TcpAction();

    virtual bool AddListener(std::shared_ptr<ISocket> socket) override;
    virtual bool AddReceiver(std::shared_ptr<ISocket> socket) override;
    virtual bool AddSender(std::shared_ptr<ISocket> socket) override;
    virtual void Remove(std::shared_ptr<ISocket> socket) override;

    virtual void Wait(uint32_t timeout_ms) override;
    virtual void Wakeup() override;

private:
    int pipe_[2];
    int32_t kqueue_handler_;

    struct kevent pipe_content_;
    std::vector<struct kevent> active_list_;
    std::unordered_set<uint64_t> listener_set_;
    std::unordered_map<uint64_t, std::weak_ptr<ISocket>> socket_map_;
};

}
}

#endif // _WIN32
#endif