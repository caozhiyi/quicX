#ifndef QUIC_INCLUDE_IF_QUIC_BIDRECTION_STREAM
#define QUIC_INCLUDE_IF_QUIC_BIDRECTION_STREAM

#include <quicx/quic/type.h>
#include <quicx/quic/if_quic_stream.h>
#include <quicx/common/if_buffer_write.h>

namespace quicx {

/**
 * @brief Bidirectional stream interface.
 *
 * Combines the send and receive halves into a single object, mirroring HTTP/3's
 * request/response patterns.
 */
class IQuicBidirectionStream:
    public virtual IQuicStream {
public:
    IQuicBidirectionStream() {}
    virtual ~IQuicBidirectionStream() {}

    virtual StreamDirection GetDirection() = 0;
    virtual uint64_t GetStreamID() = 0;

    /** Complete the stream after in-flight data has been delivered. */
    virtual void Close() = 0;

    /** Abort the stream immediately and inform the peer. */
    virtual void Reset(uint32_t error) = 0;

    /** Send raw bytes or buffered data to the peer. */
    virtual int32_t Send(uint8_t* data, uint32_t len) = 0;
    virtual int32_t Send(std::shared_ptr<IBufferRead> buffer) = 0;
    virtual std::shared_ptr<IBufferWrite> GetSendBuffer() = 0;
    virtual bool Flush() = 0;

    /**
     * @brief Callback invoked when queued data transitions to a sendable state.
     *
     * Useful for advanced flow-control or telemetry scenarios.
     */
    virtual void SetStreamWriteCallBack(stream_write_callback cb) = 0;

    /**
     * @brief Provide the handler that consumes inbound data.
     *
     * The handler executes on the receive thread; keep work minimal and offload
     * heavy lifting to other threads if necessary.
     */
    virtual void SetStreamReadCallBack(stream_read_callback cb) = 0;

    /**
     * @brief Number of bytes currently buffered inside the stream awaiting
     * transmission to the wire (i.e. queued in the send buffer but not yet
     * packed into an outgoing STREAM frame).
     *
     * Streaming producers can consult this to apply application-level
     * backpressure: stop pushing more bytes from the upstream source while
     * the in-stream queue exceeds a threshold, so quicX does not eagerly
     * buffer the entire payload in memory. See ReqRespBaseStream::HandleSent.
     */
    virtual uint64_t GetPendingSendBytes() = 0;
};

}

#endif