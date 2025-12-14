#ifndef QUIC_CONNECTION_STREAM_MANAGER_H
#define QUIC_CONNECTION_STREAM_MANAGER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <unordered_map>

#include "common/buffer/if_buffer.h"
#include "common/network/if_event_loop.h"

#include "quic/connection/controler/connection_flow_control.h"
#include "quic/connection/controler/send_manager.h"
#include "quic/connection/transport_param.h"
#include "quic/crypto/if_cryptographer.h"
#include "quic/frame/if_frame.h"
#include "quic/include/type.h"
#include "quic/stream/if_stream.h"

namespace quicx {
namespace quic {

/**
 * @brief Stream manager for QUIC stream lifecycle management
 *
 * Responsibilities:
 * - Stream creation (sync and async)
 * - Stream closure
 * - Stream limit enforcement
 * - Pending stream request queue management
 * - Stream data ACK notifications
 */
class StreamManager {
public:
    using StreamCreationCallback = std::function<void(std::shared_ptr<IStream>, uint32_t error)>;
    using StreamStateCallback = std::function<void(std::shared_ptr<IStream> stream, uint32_t error)>;
    using ToSendFrameCallback = std::function<void(std::shared_ptr<IFrame>)>;
    using ActiveSendStreamCallback = std::function<void(std::shared_ptr<IStream>)>;
    using InnerStreamCloseCallback = std::function<void(uint64_t stream_id)>;
    using InnerConnectionCloseCallback =
        std::function<void(uint64_t error, uint16_t frame_type, const std::string& reason)>;

    StreamManager(std::shared_ptr<::quicx::common::IEventLoop> event_loop, ConnectionFlowControl& flow_control,
        TransportParam& transport_param, SendManager& send_manager, StreamStateCallback stream_state_cb,
        ToSendFrameCallback to_send_frame_cb, ActiveSendStreamCallback active_send_stream_cb,
        InnerStreamCloseCallback inner_stream_close_cb, InnerConnectionCloseCallback inner_connection_close_cb);

    ~StreamManager() = default;

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

    /**
     * @brief Send a frame to waiting send list
     * @param frame Frame to send
     */
    void ToSendFrame(std::shared_ptr<IFrame> frame);

    /**
     * @brief Add a stream to active send list
     * @param stream Stream to add
     */
    void ActiveStream(std::shared_ptr<IStream> stream);

    /**
     * @brief Get send data from stream
     * @param buffer Buffer to store send data
     * @param encrypto_level Encryption level
     * @param cryptographer Cryptographer
     * @return True if send data is available, false otherwise
     */
    bool GetSendData(
        std::shared_ptr<common::IBuffer> buffer, uint8_t encrypto_level, std::shared_ptr<ICryptographer> cryptographer);

private:
    ActiveStreamSet& GetReadActiveSendStreamSet();
    ActiveStreamSet& GetWriteActiveSendStreamSet();
    void SwitchActiveSendStreamSet();

    std::shared_ptr<IPacket> MakePacket(
        IFrameVisitor* visitor, uint8_t encrypto_level, std::shared_ptr<ICryptographer> cryptographer);
    bool PacketInit(std::shared_ptr<IPacket>& packet, std::shared_ptr<common::IBuffer> buffer);
    bool PacketInit(std::shared_ptr<IPacket>& packet, std::shared_ptr<common::IBuffer> buffer, IFrameVisitor* visitor);

private:
    // Stream map
    std::unordered_map<uint64_t, std::shared_ptr<IStream>> streams_map_;

    // Pending stream creation requests
    struct PendingStreamRequest {
        StreamDirection type;
        stream_creation_callback callback;
    };
    std::queue<PendingStreamRequest> pending_stream_requests_;
    std::mutex pending_streams_mutex_;

    // Dependencies (injected)
    std::shared_ptr<::quicx::common::IEventLoop> event_loop_;
    ConnectionFlowControl& flow_control_;
    TransportParam& transport_param_;
    SendManager& send_manager_;

    // Callbacks
    StreamStateCallback stream_state_cb_;
    ToSendFrameCallback to_send_frame_cb_;
    ActiveSendStreamCallback active_send_stream_cb_;
    InnerStreamCloseCallback inner_stream_close_cb_;
    InnerConnectionCloseCallback inner_connection_close_cb_;

    std::list<std::shared_ptr<IFrame>> wait_frame_list_;

    // Dual-buffer for active streams (similar to Worker's active_send_connection_set)
    // This prevents race conditions when sended_cb_ callback adds streams back to the queue
    // while MakePacket is processing and removing streams from the queue.
    bool active_send_stream_set_1_is_current_;
    ActiveStreamSet active_send_stream_set_1_;
    ActiveStreamSet active_send_stream_set_2_;
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_STREAM_MANAGER_H
