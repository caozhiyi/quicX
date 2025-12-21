#ifndef QUIC_CONNECTION_IF_CONNECTION_EVENT_SINK
#define QUIC_CONNECTION_IF_CONNECTION_EVENT_SINK

#include <cstdint>
#include <memory>
#include <string>

namespace quicx {
namespace quic {

// Forward declarations
class IStream;
class IFrame;

/**
 * @brief Event sink interface for connection-level events
 *
 * This interface replaces callback-based event notification with direct method calls,
 * reducing complexity and improving performance by eliminating std::bind overhead.
 *
 * Components like StreamManager and FrameProcessor should hold a reference to this
 * interface instead of multiple callback functions.
 *
 * Typical implementation: BaseConnection implements this interface and passes itself
 * to child components.
 */
class IConnectionEventSink {
public:
    virtual ~IConnectionEventSink() = default;

    /**
     * @brief Notify that a stream has data ready to send
     *
     * Called by StreamManager when a stream becomes active and has pending data.
     *
     * @param stream The stream with data to send
     */
    virtual void OnStreamDataReady(std::shared_ptr<IStream> stream) = 0;

    /**
     * @brief Notify that a frame is ready to send
     *
     * Called by various components when they generate a frame that needs to be sent
     * (e.g., MAX_DATA, ACK, CONNECTION_CLOSE, etc.)
     *
     * @param frame The frame to send
     */
    virtual void OnFrameReady(std::shared_ptr<IFrame> frame) = 0;

    /**
     * @brief Notify that the connection should schedule a send operation
     *
     * Called when there is data/frames to send and the connection should be
     * added to the active send set.
     */
    virtual void OnConnectionActive() = 0;

    /**
     * @brief Notify that a stream should be closed
     *
     * Called by StreamManager when a stream completes or encounters an error.
     *
     * @param stream_id The ID of the stream to close
     */
    virtual void OnStreamClosed(uint64_t stream_id) = 0;

    /**
     * @brief Notify that the connection should be closed
     *
     * Called when a fatal error occurs (e.g., protocol violation, flow control error)
     *
     * @param error QUIC error code
     * @param frame_type Frame type that triggered the error (0 if not applicable)
     * @param reason Human-readable error description
     */
    virtual void OnConnectionClose(uint64_t error, uint16_t frame_type, const std::string& reason) = 0;
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_IF_CONNECTION_EVENT_SINK
