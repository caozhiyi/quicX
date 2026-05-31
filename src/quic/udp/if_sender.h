#ifndef QUIC_UDP_IF_SENDER
#define QUIC_UDP_IF_SENDER

#include <cstdint>
#include <vector>
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
     * @brief Send a batch of packets to the network in one syscall when possible.
     *
     * On Linux this maps to sendmmsg(2); on platforms without native sendmmsg
     * the underlying common::SendmMsg implementation falls back to a sendmsg
     * loop. Implementations are free to fall back to per-packet Send() if any
     * precondition is not met (e.g. cached sockaddr unavailable, fault
     * injection enabled, packets straddle multiple sockets).
     *
     * Semantics:
     *   - Order: FIFO within the batch (matches the order produced by the
     *     caller's drain loop).
     *   - Returns the number of packets that were successfully handed to the
     *     kernel. On short-write the trailing packets are dropped (UDP is
     *     unreliable so the QUIC layer's existing loss detection / retransmit
     *     paths handle this correctly).
     *   - The vector is consumed but not cleared; the caller may reuse its
     *     storage on the next round.
     *
     * @param batch Packets to send.
     * @return number of packets successfully transmitted.
     */
    virtual uint32_t SendBatch(std::vector<std::shared_ptr<NetPacket>>& batch) = 0;

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
