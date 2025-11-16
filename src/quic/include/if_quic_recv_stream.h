#ifndef QUIC_INCLUDE_IF_QUIC_RECV_STREAM
#define QUIC_INCLUDE_IF_QUIC_RECV_STREAM

#include "quic/include/if_quic_stream.h"

namespace quicx {

/**
 * @brief Receive-only stream interface.
 *
 * Receive streams are created implicitly by the remote peer. Applications must
 * install the read callback immediately after obtaining the stream; otherwise
 * inbound data may be dropped.
 */
class IQuicRecvStream:
    public virtual IQuicStream {
public:
    IQuicRecvStream() {}
    virtual ~IQuicRecvStream() {}

    virtual StreamDirection GetDirection() = 0;
    virtual uint64_t GetStreamID() = 0;

    /**
     * @brief Abort the stream and inform the peer with an error code.
     *
     * Pending data may be discarded locally.
     */
    virtual void Reset(uint32_t error) = 0;

    /**
     * @brief Provide the handler invoked when data arrives.
     *
     * The callback executes on the connection's receive thread; keep work
     * lightweight to avoid blocking packet processing.
     */
    virtual void SetStreamReadCallBack(stream_read_callback cb) = 0;
};

}

#endif