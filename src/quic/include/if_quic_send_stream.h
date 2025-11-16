#ifndef QUIC_INCLUDE_IF_QUIC_SEND_STREAM
#define QUIC_INCLUDE_IF_QUIC_SEND_STREAM

#include "quic/include/if_quic_stream.h"
#include "common/include/if_buffer_read.h"
#include "common/include/if_buffer_write.h"

namespace quicx {

/**
 * @brief Send-capable stream interface.
 *
 * Local endpoints explicitly create send streams. The remote peer cannot open
 * them; instead it receives notifications when the stream ID becomes visible.
 */
class IQuicSendStream:
    public virtual IQuicStream {
public:
    IQuicSendStream() {}
    virtual ~IQuicSendStream() {}

    virtual StreamDirection GetDirection() = 0;
    virtual uint64_t GetStreamID() = 0;

    /** Gracefully finish the stream once pending data is delivered. */
    virtual void Close() = 0;

    /**
     * @brief Abort the stream and notify the peer with an error.
     *
     * Outstanding data is discarded locally and not retransmitted.
     */
    virtual void Reset(uint32_t error) = 0;

    /**
     * @brief Write a memory buffer into the stream.
     *
     * @return Number of bytes accepted into the send pipeline.
     */
    virtual int32_t Send(uint8_t* data, uint32_t len) = 0;
    virtual int32_t Send(std::shared_ptr<IBufferRead> buffer) = 0;
    /**
     * @brief Get the buffer to write data to the stream.
     * 
     * @return The buffer to write data to the stream. then call Flush() to flush the data to the wire.
     */
    virtual std::shared_ptr<IBufferWrite> GetSendBuffer() = 0;
    /**
     * @brief Flush the data to the wire.
     *
     * The data is flushed to the wire and the callback is called.
     */
    virtual bool Flush() = 0;

    /**
     * @brief Install a callback that fires whenever data is flushed to the wire.
     *
     * The callback runs on a send-thread context; avoid blocking operations.
     */
    virtual void SetStreamWriteCallBack(stream_write_callback cb) = 0;
};

}

#endif