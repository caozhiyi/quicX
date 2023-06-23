#ifndef QUIC_UDP_RECEIVER
#define QUIC_UDP_RECEIVER

#include <string>
#include <cstdint>
#include "quic/udp/udp_packet_in.h"

namespace quicx {

class UdpReceiver{
public:
    UdpReceiver();
    ~UdpReceiver();

    bool Listen(const std::string& ip, uint16_t port);
    void SetRecvSocket(uint64_t sock) { _recv_sock = sock; }

    enum RecvRet {
        RR_SUCCESS    = 0,
        RR_FAILED     = 1,
        RR_WOULDBLOCK = 2,
    };
    RecvRet DoRecv(std::shared_ptr<UdpPacketIn> udp_packet);

private:
    uint64_t _recv_sock;
    Address _listen_address;
};

}

#endif