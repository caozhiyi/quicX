#ifndef QUIC_PACKET_HEADER_IF_HEADER
#define QUIC_PACKET_HEADER_IF_HEADER


#include <cstdint>

#include "common/buffer/if_buffer.h"
#include "common/buffer/shared_buffer_span.h"

#include "quic/packet/header/header_flag.h"

namespace quicx {
namespace quic {

/**
 * @brief Interface for QUIC packet headers
 *
 * Base interface for both long and short packet headers.
 */
class IHeader: public HeaderFlag {
public:
    IHeader() {}
    IHeader(PacketHeaderType type): HeaderFlag(type) {}
    IHeader(uint8_t flag): HeaderFlag(flag) {}
    virtual ~IHeader() {}

    /**
     * @brief Encode the header into a buffer
     *
     * @param buffer Destination buffer
     * @return true if encoded successfully, false otherwise
     */
    virtual bool EncodeHeader(std::shared_ptr<common::IBuffer> buffer) = 0;

    /**
     * @brief Decode header from a buffer
     *
     * @param buffer Source buffer
     * @param with_flag Whether buffer includes header flags
     * @return true if decoded successfully, false otherwise
     */
    virtual bool DecodeHeader(std::shared_ptr<common::IBuffer> buffer, bool with_flag = false) = 0;

    /**
     * @brief Calculate encoded header size
     *
     * @return Number of bytes required to encode
     */
    virtual uint32_t EncodeHeaderSize() = 0;

    /**
     * @brief Set destination connection ID
     *
     * @param id Connection ID bytes
     * @param len Connection ID length
     */
    virtual void SetDestinationConnectionId(const uint8_t* id, uint8_t len) = 0;

    /**
     * @brief Get destination connection ID length
     *
     * @return Connection ID length in bytes
     */
    virtual uint8_t GetDestinationConnectionIdLength() = 0;
    
    /**
     * @brief Get raw header data
     *
     * @return Buffer span containing header bytes
     */
    virtual common::SharedBufferSpan& GetHeaderSrcData() { return header_src_data_; }

protected:
    common::SharedBufferSpan header_src_data_;
};

}
}

#endif