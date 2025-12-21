#ifndef QUIC_CONNECTION_STREAM_MANAGER_H
#define QUIC_CONNECTION_STREAM_MANAGER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <unordered_map>

#include "common/structure/double_buffer.h"
#include "quic/include/type.h"

namespace quicx {

// Forward declarations from common namespace
namespace common {
class IEventLoop;
}

namespace quic {

// Forward declarations
class IStream;
class IFrame;
class IFrameVisitor;
class IConnectionEventSink;
class SendFlowController;
class SendManager;
class TransportParam;

/**
 * @brief Stream manager for QUIC stream lifecycle management
 *
 * Responsibilities:
 * - Stream creation (sync and async)
 * - Stream closure
 * - Stream limit enforcement
 * - Pending stream request queue management
 * - Stream data ACK notifications
 *
 * Refactored (Phase 3): Uses IConnectionEventSink interface instead of callbacks
 * to reduce std::bind overhead and improve performance.
 */
class StreamManager {
public:
    using StreamCreationCallback = std::function<void(std::shared_ptr<IStream>, uint32_t error)>;
    using StreamStateCallback = std::function<void(std::shared_ptr<IStream> stream, uint32_t error)>;

    StreamManager(IConnectionEventSink& event_sink, std::shared_ptr<::quicx::common::IEventLoop> event_loop,
        TransportParam& transport_param, SendManager& send_manager, StreamStateCallback stream_state_cb,
        SendFlowController* send_flow_controller);

    ~StreamManager() {
        // Clear callback to prevent use-after-free
        stream_state_cb_ = nullptr;
    }

    // ==================== Stream Creation ====================

    /**
     * @brief Create stream with flow control and limit checks (public API)
     * @param type Stream direction
     * @return Stream instance or nullptr if limit reached
     */
    std::shared_ptr<IStream> MakeStreamWithFlowControl(StreamDirection type);

    /**
     * @brief Create stream asynchronously (queues if limit reached)
     * @param type Stream direction
     * @param callback Callback invoked when stream is created
     * @return true if created immediately, false if queued
     */
    bool MakeStreamAsync(StreamDirection type, stream_creation_callback callback);

    /**
     * @brief Retry pending stream creation requests (called after MAX_STREAMS received)
     */
    void RetryPendingStreamRequests();

    /**
     * @brief Create and register stream (internal, called after flow control checks)
     * @param init_size Initial flow control window size
     * @param stream_id Stream ID
     * @param type Stream direction
     * @return Stream instance or nullptr on error
     */
    std::shared_ptr<IStream> MakeStream(uint32_t init_size, uint64_t stream_id, StreamDirection type);

    // ==================== Stream Closure ====================

    /**
     * @brief Close a stream
     * @param stream_id Stream ID to close
     */
    void CloseStream(uint64_t stream_id);

    // ==================== Stream Lookup ====================

    /**
     * @brief Find stream by ID
     * @param stream_id Stream ID
     * @return Stream instance or nullptr if not found
     */
    std::shared_ptr<IStream> FindStream(uint64_t stream_id);

    // ==================== Remote Stream Creation ====================

    /**
     * @brief Create stream initiated by remote peer
     * @param init_size Initial flow control window size
     * @param stream_id Stream ID from remote
     * @param direction Stream direction
     * @return Stream instance or nullptr on error
     */
    std::shared_ptr<IStream> CreateRemoteStream(uint32_t init_size, uint64_t stream_id, StreamDirection direction);

    // ==================== Stream ACK Notification ====================

    /**
     * @brief Notify stream that data has been ACKed
     * @param stream_id Stream ID
     * @param max_offset Maximum offset ACKed
     * @param has_fin Whether FIN bit was ACKed
     */
    void OnStreamDataAcked(uint64_t stream_id, uint64_t max_offset, bool has_fin);

    // ==================== Stream Reset ====================

    /**
     * @brief Reset all streams (called during connection close)
     * @param error Error code to send to streams
     */
    void ResetAllStreams(uint64_t error);

    /**
     * @brief Get all stream IDs (for iteration)
     * @return Vector of stream IDs
     */
    std::vector<uint64_t> GetAllStreamIDs() const;

    // ==================== Stream Scheduling (Week 4 Refactoring) ====================

    /**
     * @brief Mark stream as active for sending
     *
     * Adds stream to the active streams queue for scheduling. Uses double-buffer
     * mechanism to avoid concurrency issues during packet building.
     *
     * @param stream Stream to mark as active
     */
    void MarkStreamActive(std::shared_ptr<IStream> stream);

    /**
     * @brief Build STREAM frames for active streams
     *
     * Iterates through active streams and builds STREAM frames for packet.
     * Handles encryption level filtering and flow control limits.
     *
     * @param visitor Frame visitor to receive STREAM frames
     * @param encrypto_level Current encryption level
     * @return true if more stream data pending, false otherwise
     */
    bool BuildStreamFrames(IFrameVisitor* visitor, uint8_t encrypto_level);

    /**
     * @brief Clear all active streams (called during connection close)
     */
    void ClearActiveStreams();

    /**
     * @brief Check if there are active streams pending send
     * @return true if active streams exist, false otherwise
     */
    bool HasActiveStreams() const {
        return !active_streams_.IsEmpty();
    }

private:
    // Stream map
    std::unordered_map<uint64_t, std::shared_ptr<IStream>> streams_map_;

    // Active streams (Week 4 refactoring) - uses double-buffer for concurrency safety
    common::DoubleBuffer<std::shared_ptr<IStream>> active_streams_;

    // Pending stream creation requests
    struct PendingStreamRequest {
        StreamDirection type;
        stream_creation_callback callback;
    };
    std::queue<PendingStreamRequest> pending_stream_requests_;
    std::mutex pending_streams_mutex_;

    // Dependencies (injected)
    IConnectionEventSink& event_sink_;  // Event interface (replaces callbacks)
    std::shared_ptr<::quicx::common::IEventLoop> event_loop_;
    SendFlowController* send_flow_controller_;  // Send-side flow controller
    TransportParam& transport_param_;
    SendManager& send_manager_;

    // External callback (for application notification)
    StreamStateCallback stream_state_cb_;
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_STREAM_MANAGER_H
