#ifndef QUIC_UDP_ACTION_KQUEUE_UDP_ACTION
#define QUIC_UDP_ACTION_KQUEUE_UDP_ACTION

#ifdef __APPLE__

#include <queue>
#include <sys/types.h>
#include <sys/event.h>
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

    virtual void Wakeup();
    
private:
    int pipe_[2];
    int32_t kqueue_handler_;
    std::unordered_map<uint64_t, struct kevent> kqueue_event_map_;

    struct kevent pipe_content_;
    std::vector<struct kevent> active_list_;
};

}
}

#endif // _WIN32
#endif