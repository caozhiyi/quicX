#ifndef QUIC_CONNECTION_PACKET_VISITOR_INTERFACE
#define QUIC_CONNECTION_PACKET_VISITOR_INTERFACE

#include "quic/packet/if_packet.h"
#include "common/buffer/if_buffer.h"

namespace quicx {
namespace quic {

/**
 * @brief Visitor interface for processing received QUIC packets
 *
 * Implementations handle incoming packets and manage buffer state.
 */
class IPacketVisitor {
public:
    IPacketVisitor() {}
    virtual ~IPacketVisitor() {}

    /**
     * @brief Process a received packet
     *
     * @param packet The packet to handle
     * @return true if handled successfully, false otherwise
     */
    virtual bool HandlePacket(std::shared_ptr<IPacket> packet) = 0;

    /**
     * @brief Get remaining buffer space
     *
     * @return Number of bytes left in buffer
     */
    virtual uint32_t GetLeftSize() = 0;

    /**
     * @brief Get the underlying buffer
     *
     * @return Buffer instance used by this visitor
     */
    virtual std::shared_ptr<common::IBuffer> GetBuffer() = 0;
};

}
}

#endif