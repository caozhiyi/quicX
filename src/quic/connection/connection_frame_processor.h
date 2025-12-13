#ifndef QUIC_CONNECTION_FRAME_PROCESSOR_H
#define QUIC_CONNECTION_FRAME_PROCESSOR_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "quic/frame/if_frame.h"

namespace quicx {
namespace quic {

// Forward declarations
class ConnectionStateMachine;
class ConnectionCrypto;
class FlowControl;
class SendManager;
class StreamManager;
class ConnectionIDCoordinator;
class PathManager;
class ConnectionCloser;
class TransportParam;
class IStream;

/**
 * @brief Frame processor for handling all QUIC frame types
 *
 * Responsibilities:
 * - Dispatch frames to appropriate handlers
 * - Process stream-related frames (STREAM, RESET_STREAM, STOP_SENDING, etc.)
 * - Process connection-level frames (ACK, CONNECTION_CLOSE, etc.)
 * - Process flow control frames (MAX_DATA, MAX_STREAMS, etc.)
 * - Process path validation frames (PATH_CHALLENGE, PATH_RESPONSE)
 * - Process connection ID frames (NEW_CONNECTION_ID, RETIRE_CONNECTION_ID)
 */
class FrameProcessor {
public:
    // Callbacks for cross-module communication
    using ToSendFrameCallback = std::function<void(std::shared_ptr<IFrame>)>;
    using ActiveSendCallback = std::function<void()>;
    using InnerConnectionCloseCallback =
        std::function<void(uint64_t error, uint16_t frame_type, const std::string& reason)>;
    using StreamStateCallback = std::function<void(std::shared_ptr<IStream>, uint32_t error)>;
    using RetryPendingStreamRequestsCallback = std::function<void()>;
    using HandshakeDoneCallback = std::function<bool(std::shared_ptr<IFrame>)>;

    FrameProcessor(ConnectionStateMachine& state_machine, ConnectionCrypto& connection_crypto,
        FlowControl& flow_control, SendManager& send_manager, StreamManager& stream_manager,
        ConnectionIDCoordinator& cid_coordinator, PathManager& path_manager, ConnectionCloser& connection_closer,
        TransportParam& transport_param, std::string& token);

    ~FrameProcessor() = default;

    // ==================== Frame Dispatching ====================

    /**
     * @brief Dispatch frames to appropriate handlers
     * @param frames Vector of frames to process
     * @param crypto_level Encryption level (for ACK frame handling)
     * @return true if all frames processed successfully
     */
    bool OnFrames(std::vector<std::shared_ptr<IFrame>>& frames, uint16_t crypto_level);

    // ==================== Callback Management ====================

    /**
     * @brief Set callback for sending frames
     */
    void SetToSendFrameCallback(ToSendFrameCallback cb) { to_send_frame_cb_ = cb; }

    /**
     * @brief Set callback for active sending
     */
    void SetActiveSendCallback(ActiveSendCallback cb) { active_send_cb_ = cb; }

    /**
     * @brief Set callback for connection close
     */
    void SetInnerConnectionCloseCallback(InnerConnectionCloseCallback cb) { inner_connection_close_cb_ = cb; }

    /**
     * @brief Set callback for stream state change
     */
    void SetStreamStateCallback(StreamStateCallback cb) { stream_state_cb_ = cb; }

    /**
     * @brief Set callback for retrying pending stream requests
     */
    void SetRetryPendingStreamRequestsCallback(RetryPendingStreamRequestsCallback cb) {
        retry_pending_stream_requests_cb_ = cb;
    }

    /**
     * @brief Set callback for handshake done (client-only)
     */
    void SetHandshakeDoneCallback(HandshakeDoneCallback cb) { handshake_done_cb_ = cb; }

private:
    // ==================== Frame Handlers ====================

    bool OnStreamFrame(std::shared_ptr<IFrame> frame);
    bool OnAckFrame(std::shared_ptr<IFrame> frame, uint16_t crypto_level);
    bool OnCryptoFrame(std::shared_ptr<IFrame> frame);
    bool OnNewTokenFrame(std::shared_ptr<IFrame> frame);
    bool OnMaxDataFrame(std::shared_ptr<IFrame> frame);
    bool OnDataBlockFrame(std::shared_ptr<IFrame> frame);
    bool OnStreamBlockFrame(std::shared_ptr<IFrame> frame);
    bool OnMaxStreamFrame(std::shared_ptr<IFrame> frame);
    bool OnNewConnectionIDFrame(std::shared_ptr<IFrame> frame);
    bool OnRetireConnectionIDFrame(std::shared_ptr<IFrame> frame);
    bool OnConnectionCloseFrame(std::shared_ptr<IFrame> frame);
    bool OnConnectionCloseAppFrame(std::shared_ptr<IFrame> frame);
    bool OnPathChallengeFrame(std::shared_ptr<IFrame> frame);
    bool OnPathResponseFrame(std::shared_ptr<IFrame> frame);

    // Dependencies (injected references)
    ConnectionStateMachine& state_machine_;
    ConnectionCrypto& connection_crypto_;
    FlowControl& flow_control_;
    SendManager& send_manager_;
    StreamManager& stream_manager_;
    ConnectionIDCoordinator& cid_coordinator_;
    PathManager& path_manager_;
    ConnectionCloser& connection_closer_;
    TransportParam& transport_param_;
    std::string& token_;

    // Callbacks
    ToSendFrameCallback to_send_frame_cb_;
    ActiveSendCallback active_send_cb_;
    InnerConnectionCloseCallback inner_connection_close_cb_;
    StreamStateCallback stream_state_cb_;
    RetryPendingStreamRequestsCallback retry_pending_stream_requests_cb_;
    HandshakeDoneCallback handshake_done_cb_;
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_FRAME_PROCESSOR_H
