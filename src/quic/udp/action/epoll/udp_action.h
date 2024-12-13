#ifndef QUIC_UDP_ACTION_EPOLL_UDP_ACTION
#define QUIC_UDP_ACTION_EPOLL_UDP_ACTION

#ifdef __linux__

#include <queue>
#include <sys/epoll.h>
#include <unordered_map>
#include "quic/udp/action/if_udp_action.h"

namespace quicx {
namespace quic {

class UdpAction:
    public IUdpAction {
public:
    UdpAction();
    virtual ~UdpAction();

    virtual bool AddSocket(uint64_t socket);

    virtual void RemoveSocket(uint64_t socket);

    virtual void Wait(int32_t timeout_ms, std::queue<uint64_t>& sockets);

    virtual void WeakUp();
    
private:
    uint32_t pipe_[2];
    int32_t epoll_handler_;
    std::unordered_map<uint64_t, epoll_event> epoll_event_map_;

    epoll_event pipe_content_;
    std::vector<epoll_event> active_list_;
};

}
}

#endif // __linux__
#endif