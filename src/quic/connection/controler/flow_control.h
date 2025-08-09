#ifndef QUIC_CONNECTION_CONTROLER_FLOW_CONTROL
#define QUIC_CONNECTION_CONTROLER_FLOW_CONTROL

#include "quic/frame/if_frame.h"
#include "quic/stream/stream_id_generator.h"
#include "quic/connection/transport_param.h"

namespace quicx {
namespace quic {

// flow control.
// include limit number of stream and max offset of connection.
class FlowControl {
public:
    FlowControl(StreamIDGenerator::StreamStarter starter);
    ~FlowControl() {}

    // set init flow control from transport param
    void UpdateConfig(const TransportParam& tp);

    // check local send data limit
    // add local send data offset when send data
    void AddLocalSendData(uint32_t size);
    // add local send data limit when receive max stream data frame
    void AddLocalSendDataLimit(uint64_t limit);
    // check local send data limit, return true if can send data
    // if return true, can_send_size is the size of data can send
    // send_frame is the stream data block frame to send
    bool CheckLocalSendDataLimit(uint64_t& can_send_size, std::shared_ptr<IFrame>& send_frame);

    // check remote send data 
    // add remote send data offset when receive stream data frame
    void AddRemoteSendData(uint32_t size);
    // check if remote send data limit, return true if can send data
    // send_frame is the stream data block frame to send
    bool CheckRemoteSendDataLimit(std::shared_ptr<IFrame>& send_frame);

    // check count of local stream limit
    // add local bidirection stream limit when receive max streams frame
    void AddLocalBidirectionStreamLimit(uint64_t limit);    
    // check local bidirection stream limit, return true if not limit
    // send_frame is the stream block frame to send
    bool CheckLocalBidirectionStreamLimit(uint64_t& stream_id, std::shared_ptr<IFrame>& send_frame);
    // add local unidirection stream limit when receive max streams frame
    void AddLocalUnidirectionStreamLimit(uint64_t limit);
    // check local unidirection stream limit, return true if not limit
    // send_frame is the stream block frame to send
    bool CheckLocalUnidirectionStreamLimit(uint64_t& stream_id, std::shared_ptr<IFrame>& send_frame);

    // check remote stream limit
    // send_frame is the stream block frame to send
    bool CheckRemoteStreamLimit(uint64_t id, std::shared_ptr<IFrame>& send_frame) ;
private:
    bool CheckRemoteBidirectionStreamLimit(std::shared_ptr<IFrame>& send_frame);
    bool CheckRemoteUnidirectionStreamLimit(std::shared_ptr<IFrame>& send_frame);

private:
    // data flow control
    uint64_t local_send_max_data_limit_;
    uint64_t local_send_data_size_;
    uint64_t remote_send_max_data_limit_;
    uint64_t remote_send_data_size_;
    // streams flow control
    uint64_t local_max_bidirectional_stream_id_;
    uint64_t local_bidirectional_stream_limit_;
    uint64_t local_max_unidirectional_stream_id_;
    uint64_t local_unidirectional_stream_limit_;
    
    uint64_t remote_max_bidirectional_stream_id_;
    uint64_t remote_bidirectional_stream_limit_;
    uint64_t remote_max_unidirectional_stream_id_;
    uint64_t remote_unidirectional_stream_limit_;

    StreamIDGenerator id_generator_;
};

}
}

#endif