#ifndef QUIC_UDP_ACTION_SELECT_UDP_ACTION
#define QUIC_UDP_ACTION_SELECT_UDP_ACTION

#ifdef _WIN32

#include <queue>
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
    uint64_t pipe_[2];
    int32_t select_handler_;
    std::unordered_map<uint64_t, WSAPOLLFD> select_event_map_;

    WSAPOLLFD pipe_content_;
    std::vector<WSAPOLLFD> active_list_;
};

}
}

#endif // _WIN32
#endif