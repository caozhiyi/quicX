#ifndef QUIC_INCLUDE_IF_STREAM
#define QUIC_INCLUDE_IF_STREAM

#include "quic/include/type.h"

namespace quicx {

/**
 * @brief Base interface shared by all QUIC stream variants.
 *
 * Streams encapsulate ordered byte-flows multiplexed over a single connection.
 * Concrete sub-interfaces extend the base with send/receive primitives.
 */
class IQuicStream {
public:
    IQuicStream() {}
    virtual ~IQuicStream() {}

    /**
     * @brief Abruptly terminate the stream.
     *
     * @param error Application error code sent to the peer.
     */
    virtual void Reset(uint32_t error) = 0;
    /** Direction (send-only, recv-only, bidirectional). */
    virtual StreamDirection GetDirection() = 0;
    /** Stream ID unique within the owning connection. */
    virtual uint64_t GetStreamID() = 0;
};

}

#endif