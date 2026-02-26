#ifndef QUIC_UDP_IF_SENDER
#define QUIC_UDP_IF_SENDER

#include <cstdint>
#include "quic/udp/net_packet.h"

namespace quicx {
namespace quic {

/**
 * @brief Sender interface for transmitting packets to the network
 */
class ISender {
public:
    ISender() {}
    virtual ~ISender() {}

    /**
     * @brief Send a packet to the network
     *
     * @param pkt Packet to send
     * @return true if sent successfully, false otherwise
     */
    virtual bool Send(std::shared_ptr<NetPacket>& pkt) = 0;

    /**
     * @brief Get the underlying socket file descriptor
     *
     * @return Socket file descriptor
     */
    virtual int32_t GetSocket() const = 0;

    /**
     * @brief Create a sender instance
     *
     * @param sockfd Socket file descriptor, -1 to create new socket
     * @return Sender instance
     */
    static std::shared_ptr<ISender> MakeSender(int32_t sockfd = -1);
};

}
}

#endif
