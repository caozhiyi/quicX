#ifndef QUIC_CONNECTION_CONTROLER_SEND_FLOW_CONTROLLER
#define QUIC_CONNECTION_CONTROLER_SEND_FLOW_CONTROLLER

#include <cstdint>
#include <memory>

#include "quic/connection/transport_param.h"
#include "quic/frame/if_frame.h"
#include "quic/stream/stream_id_generator.h"

namespace quicx {
namespace quic {

/**
 * @brief Send-side flow controller for connection-level flow control
 *
 * This controller manages outgoing data and stream creation, enforcing limits
 * set by the peer via MAX_DATA and MAX_STREAMS frames.
 *
 * Key responsibilities:
 * 1. Track how much data we've sent (connection-level)
 * 2. Enforce peer's MAX_DATA limit on our sending
 * 3. Track how many streams we've created
 * 4. Enforce peer's MAX_STREAMS limits (bidirectional and unidirectional)
 * 5. Generate DATA_BLOCKED and STREAMS_BLOCKED frames when hitting limits
 *
 * This is the "send" half of the symmetric flow control design, complementing
 * RecvFlowController which manages incoming data.
 *
 * Thread safety: Not thread-safe. Caller must provide synchronization.
 */
class SendFlowController {
public:
    /**
     * @brief Constructor
     *
     * @param starter Stream ID generator starter (client or server)
     */
    explicit SendFlowController(StreamIDGenerator::StreamStarter starter);

    ~SendFlowController() = default;

    /**
     * @brief Update configuration from transport parameters
     *
     * Sets initial flow control limits from peer's transport parameters:
     * - initial_max_data: Connection-level send limit
     * - initial_max_streams_bidi: Maximum bidirectional streams we can create
     * - initial_max_streams_uni: Maximum unidirectional streams we can create
     *
     * @param tp Transport parameters from peer
     */
    void UpdateConfig(const TransportParam& tp);

    /**
     * @brief Record sent data (connection-level)
     *
     * Called when we send data on any stream. Updates sent byte counter.
     *
     * @param size Number of bytes sent
     */
    void OnDataSent(uint32_t size);

    /**
     * @brief Update MAX_DATA limit from peer
     *
     * Called when receiving MAX_DATA frame from peer, increasing how much
     * data we're allowed to send.
     *
     * @param limit New maximum data limit from peer
     */
    void OnMaxDataReceived(uint64_t limit);

    /**
     * @brief Check if we can send data (connection-level flow control)
     *
     * Verifies if sending is allowed based on connection-level flow control.
     * If blocked or near limit, returns appropriate frame to send.
     *
     * @param can_send_size [out] Maximum bytes we can send
     * @param blocked_frame [out] DATA_BLOCKED frame if blocked, nullptr otherwise
     * @return true if sending is allowed, false if blocked
     */
    bool CanSendData(uint64_t& can_send_size, std::shared_ptr<IFrame>& blocked_frame);

    /**
     * @brief Update MAX_STREAMS limit for bidirectional streams
     *
     * Called when receiving MAX_STREAMS (bidirectional) frame from peer.
     *
     * @param limit New maximum bidirectional stream count from peer
     */
    void OnMaxStreamsBidiReceived(uint64_t limit);

    /**
     * @brief Check if we can create a bidirectional stream
     *
     * Attempts to allocate a new bidirectional stream ID. If blocked,
     * returns STREAMS_BLOCKED frame.
     *
     * @param stream_id [out] Allocated stream ID if successful
     * @param blocked_frame [out] STREAMS_BLOCKED frame if blocked, nullptr otherwise
     * @return true if stream creation allowed, false if blocked
     */
    bool CanCreateBidiStream(uint64_t& stream_id, std::shared_ptr<IFrame>& blocked_frame);

    /**
     * @brief Update MAX_STREAMS limit for unidirectional streams
     *
     * Called when receiving MAX_STREAMS (unidirectional) frame from peer.
     *
     * @param limit New maximum unidirectional stream count from peer
     */
    void OnMaxStreamsUniReceived(uint64_t limit);

    /**
     * @brief Check if we can create a unidirectional stream
     *
     * Attempts to allocate a new unidirectional stream ID. If blocked,
     * returns STREAMS_BLOCKED frame.
     *
     * @param stream_id [out] Allocated stream ID if successful
     * @param blocked_frame [out] STREAMS_BLOCKED frame if blocked, nullptr otherwise
     * @return true if stream creation allowed, false if blocked
     */
    bool CanCreateUniStream(uint64_t& stream_id, std::shared_ptr<IFrame>& blocked_frame);

    /**
     * @brief Get current bidirectional stream limit
     *
     * @return Maximum number of bidirectional streams we can create
     */
    uint64_t GetBidiStreamLimit() const { return max_streams_bidi_; }

    /**
     * @brief Get current unidirectional stream limit
     *
     * @return Maximum number of unidirectional streams we can create
     */
    uint64_t GetUniStreamLimit() const { return max_streams_uni_; }

private:
    // Connection-level data flow control
    uint64_t sent_bytes_;              // Total bytes sent on connection
    uint64_t max_data_;                // Maximum bytes we can send (set by peer)

    // Stream creation limits (set by peer)
    uint64_t max_streams_bidi_;        // Maximum bidirectional streams we can create
    uint64_t max_streams_uni_;         // Maximum unidirectional streams we can create

    // Stream ID tracking
    uint64_t max_bidi_stream_id_;      // Highest bidirectional stream ID allocated
    uint64_t max_uni_stream_id_;       // Highest unidirectional stream ID allocated

    // Stream ID generator
    StreamIDGenerator id_generator_;

    // Flow control thresholds (TODO: make configurable)
    static constexpr uint64_t kDataBlockedThreshold = 8912;      // Bytes threshold for DATA_BLOCKED
    static constexpr uint64_t kStreamsBlockedThreshold = 4;     // Streams threshold for STREAMS_BLOCKED
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_CONTROLER_SEND_FLOW_CONTROLLER
