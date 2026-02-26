#ifndef QUIC_CONNECTION_CONTROLER_RECV_FLOW_CONTROLLER
#define QUIC_CONNECTION_CONTROLER_RECV_FLOW_CONTROLLER

#include <cstdint>
#include <memory>

#include "quic/connection/transport_param.h"
#include "quic/frame/if_frame.h"

namespace quicx {
namespace quic {

// Receive-side flow controller for connection-level flow control
// This controller manages incoming data and peer's stream creation, enforcing
// our limits and sending MAX_DATA/MAX_STREAMS frames to grant more capacity.
// Key responsibilities:
// 1. Track how much data peer has sent (connection-level)
// 2. Enforce our MAX_DATA limit on peer's sending
// 3. Track how many streams peer has created
// 4. Enforce our MAX_STREAMS limits (bidirectional and unidirectional)
// 5. Generate MAX_DATA and MAX_STREAMS frames to grant more capacity
// 6. Validate peer doesn't exceed limits (connection close if violated)
// This is the "receive" half of the symmetric flow control design, complementing
// SendFlowController which manages outgoing data.
// Thread safety: Not thread-safe. Caller must provide synchronization.
class RecvFlowController {
public:
    // Constructor
    RecvFlowController();

    ~RecvFlowController() = default;

    // Update configuration from transport parameters
    // Sets initial flow control limits from our transport parameters:
    // - initial_max_data: Connection-level receive limit
    // - initial_max_streams_bidi: Maximum bidirectional streams peer can create
    // - initial_max_streams_uni: Maximum unidirectional streams peer can create
    // @param tp Our transport parameters (advertised to peer)
    void UpdateConfig(const TransportParam& tp);

    // Record received data (connection-level)
    // Called when we receive data from peer on any stream. Updates received
    // byte counter and validates against our limit.
    // @param size Number of bytes received
    // @return true if within limit, false if peer exceeded our MAX_DATA (protocol violation)
    bool OnDataReceived(uint32_t size);

    // Check if we should send MAX_DATA frame
    // Determines if we should grant peer more send capacity. Returns MAX_DATA
    // frame if we're near the limit and should increase it.
    // @param max_data_frame [out] MAX_DATA frame if we should increase limit, nullptr otherwise
    // @return true if peer can continue sending, false if peer violated limit
    bool ShouldSendMaxData(std::shared_ptr<IFrame>& max_data_frame);

    // Validate and record peer's new stream creation
    // Called when peer creates a new stream (we receive first frame on that stream).
    // Validates stream ID against our limits and potentially sends MAX_STREAMS.
    // @param stream_id Stream ID created by peer
    // @param max_streams_frame [out] MAX_STREAMS frame if we should increase limit, nullptr otherwise
    // @return true if stream creation is valid, false if peer exceeded our MAX_STREAMS (protocol violation)
    bool OnStreamCreated(uint64_t stream_id, std::shared_ptr<IFrame>& max_streams_frame);

    // Get current maximum data limit we've advertised to peer
    // @return Maximum bytes peer is allowed to send
    uint64_t GetMaxData() const { return max_data_; }

    // Get current bidirectional stream limit we've advertised
    // @return Maximum bidirectional streams peer can create
    uint64_t GetMaxStreamsBidi() const { return max_streams_bidi_; }

    // Get current unidirectional stream limit we've advertised
    // @return Maximum unidirectional streams peer can create
    uint64_t GetMaxStreamsUni() const { return max_streams_uni_; }

private:
    // Check and potentially increase bidirectional stream limit
    // @param max_streams_frame [out] MAX_STREAMS frame if we should increase limit
    // @return true if peer hasn't exceeded limit, false if violated
    bool CheckBidiStreamLimit(std::shared_ptr<IFrame>& max_streams_frame);

    // Check and potentially increase unidirectional stream limit
    // @param max_streams_frame [out] MAX_STREAMS frame if we should increase limit
    // @return true if peer hasn't exceeded limit, false if violated
    bool CheckUniStreamLimit(std::shared_ptr<IFrame>& max_streams_frame);

private:
    // Connection-level data flow control
    uint64_t received_bytes_;  // Total bytes received on connection
    uint64_t max_data_;        // Maximum bytes peer can send (our limit)

    // Stream creation limits (our limits for peer)
    uint64_t max_streams_bidi_;  // Maximum bidirectional streams peer can create
    uint64_t max_streams_uni_;   // Maximum unidirectional streams peer can create

    // Stream ID tracking
    uint64_t max_bidi_stream_id_;  // Highest bidirectional stream ID seen from peer
    uint64_t max_uni_stream_id_;   // Highest unidirectional stream ID seen from peer
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_CONTROLER_RECV_FLOW_CONTROLLER
