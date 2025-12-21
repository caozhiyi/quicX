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
class IConnectionEventSink;
class SendFlowController;
class RecvFlowController;
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
 *
 * Refactored (Phase 3): Uses IConnectionEventSink interface instead of callbacks
 * to reduce std::bind overhead and improve performance.
 */
class FrameProcessor {
public:
    // Application-level callbacks (cannot be replaced by event interface)
    using StreamStateCallback = std::function<void(std::shared_ptr<IStream>, uint32_t error)>;
    using HandshakeDoneCallback = std::function<bool(std::shared_ptr<IFrame>)>;

    FrameProcessor(IConnectionEventSink& event_sink, ConnectionStateMachine& state_machine,
        ConnectionCrypto& connection_crypto, SendManager& send_manager, StreamManager& stream_manager,
        ConnectionIDCoordinator& cid_coordinator, PathManager& path_manager, ConnectionCloser& connection_closer,
        TransportParam& transport_param, std::string& token, SendFlowController* send_flow_controller = nullptr,
        RecvFlowController* recv_flow_controller = nullptr);

    ~FrameProcessor() = default;

    // ==================== Frame Dispatching ====================

    /**
     * @brief Dispatch frames to appropriate handlers
     * @param frames Vector of frames to process
     * @param crypto_level Encryption level (for ACK frame handling)
     * @return true if all frames processed successfully
     */
    bool OnFrames(std::vector<std::shared_ptr<IFrame>>& frames, uint16_t crypto_level);

    // ==================== Callback Management (Application-level only) ====================

    /**
     * @brief Set callback for stream state change (application notification)
     */
    void SetStreamStateCallback(StreamStateCallback cb) { stream_state_cb_ = cb; }

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
    IConnectionEventSink& event_sink_;  // Event interface (replaces most callbacks)
    ConnectionStateMachine& state_machine_;
    ConnectionCrypto& connection_crypto_;
    SendFlowController* send_flow_controller_;  // Send-side flow controller
    RecvFlowController* recv_flow_controller_;  // Recv-side flow controller
    SendManager& send_manager_;
    StreamManager& stream_manager_;
    ConnectionIDCoordinator& cid_coordinator_;
    PathManager& path_manager_;
    ConnectionCloser& connection_closer_;
    TransportParam& transport_param_;
    std::string& token_;

    // Application-level callbacks (cannot be replaced by event interface)
    StreamStateCallback stream_state_cb_;
    HandshakeDoneCallback handshake_done_cb_;
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_FRAME_PROCESSOR_H
