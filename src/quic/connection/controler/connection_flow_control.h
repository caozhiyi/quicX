#ifndef QUIC_CONNECTION_CONTROLER_FLOW_CONTROL
#define QUIC_CONNECTION_CONTROLER_FLOW_CONTROL

#include "quic/connection/transport_param.h"
#include "quic/frame/if_frame.h"
#include "quic/stream/stream_id_generator.h"

namespace quicx {
namespace quic {

// connection level flow control.
// include limit number of stream and max offset of connection.
class ConnectionFlowControl {
public:
    ConnectionFlowControl(StreamIDGenerator::StreamStarter starter);
    ~ConnectionFlowControl() {}

    // set init flow control from transport param
    void UpdateConfig(const TransportParam& tp);

    // add peer send data size when receive stream data frame
    void AddPeerControlSendData(uint32_t size);
    // add peer send data limit when receive max data frame
    void AddPeerControlSendDataLimit(uint64_t limit);
    // check peer send data limit, return true if can send data
    // if return true, can_send_size is the size of data can send
    // send_frame is the data blocked frame to send
    // if return false, sending data blocked frame to peer and send nothing.
    bool CheckPeerControlSendDataLimit(uint64_t& can_send_size, std::shared_ptr<IFrame>& send_frame);

    // add control peer send data size when receive stream data frame, return true if success
    // if return false, the connection should close by error code STREAM_LIMIT_ERROR.
    bool AddControlPeerSendData(uint32_t size);
    // check if control peer send data limit, return true if can send data
    // send_frame is the max data frame to send
    bool CheckControlPeerSendDataLimit(std::shared_ptr<IFrame>& send_frame);
    // check control peer stream limit
    // send_frame is the stream block frame to send
    bool CheckControlPeerStreamLimit(uint64_t id, std::shared_ptr<IFrame>& send_frame);
    // add peer bidirection stream limit when receive max streams frame
    void AddPeerControlBidirectionStreamLimit(uint64_t limit);
    // check local bidirection stream limit, return true if not limit
    // send_frame is the stream block frame to send
    bool CheckPeerControlBidirectionStreamLimit(uint64_t& stream_id, std::shared_ptr<IFrame>& send_frame);
    // add local unidirection stream limit when receive max streams frame
    void AddPeerControlUnidirectionStreamLimit(uint64_t limit);
    // check local unidirection stream limit, return true if not limit
    // send_frame is the stream block frame to send
    bool CheckPeerControlUnidirectionStreamLimit(uint64_t& stream_id, std::shared_ptr<IFrame>& send_frame);

    // Get current stream limits (for retry queue size validation)
    uint64_t GetPeerControlBidirectionStreamLimit() const { return peer_control_bidirectional_stream_limit_; }
    uint64_t GetPeerControlUnidirectionStreamLimit() const { return peer_control_unidirectional_stream_limit_; }

private:
    bool CheckControlPeerBidirectionStreamLimit(std::shared_ptr<IFrame>& send_frame);
    bool CheckControlPeerUnidirectionStreamLimit(std::shared_ptr<IFrame>& send_frame);

private:
    // Flow control limits set by peer (via MAX_DATA/MAX_STREAMS frames) that restrict local sending
    // These limits are enforced by the peer to prevent local endpoint from sending too much data
    uint64_t peer_control_send_max_data_limit_;
    uint64_t peer_control_send_data_size_;
    uint64_t peer_control_max_bidirectional_stream_id_;
    uint64_t peer_control_bidirectional_stream_limit_;
    uint64_t peer_control_max_unidirectional_stream_id_;
    uint64_t peer_control_unidirectional_stream_limit_;

    // Flow control limits we set (via MAX_DATA/MAX_STREAMS frames) that restrict peer sending
    // These limits are enforced by us to prevent peer from sending too much data
    uint64_t control_peer_send_max_data_limit_;
    uint64_t control_peer_send_data_size_;
    uint64_t control_peer_max_bidirectional_stream_id_;
    uint64_t control_peer_bidirectional_stream_limit_;
    uint64_t control_peer_max_unidirectional_stream_id_;
    uint64_t control_peer_unidirectional_stream_limit_;

    StreamIDGenerator id_generator_;
};

}  // namespace quic
}  // namespace quicx

#endif