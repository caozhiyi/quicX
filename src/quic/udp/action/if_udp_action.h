#ifndef QUIC_UDP_ACTION_IF_UDP_ACTION
#define QUIC_UDP_ACTION_IF_UDP_ACTION

#include <queue>
#include <cstdint>

namespace quicx {
namespace quic {

/*
 a sample udp event action interface, only support read event
*/
class IUdpAction {
public:
    IUdpAction() {}
    virtual ~IUdpAction() {}

    // add socket to action
    virtual bool AddSocket(int32_t sockfd) = 0;

    // remove socket from action
    virtual void RemoveSocket(int32_t sockfd) = 0;

    // wait for event, return the socket which has event
    // timeout_ms: wait timeout, -1 for infinite
    // sockfds: return the socket which has event
    virtual void Wait(int32_t timeout_ms, std::queue<int32_t>& sockfds) = 0;

    // notify the action to wake up
    virtual void Wakeup() = 0;
};

}
}

#endif