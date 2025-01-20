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
    void AddLocalSendData(uint32_t size);
    void UpdateLocalSendDataLimit(uint64_t limit);
    bool CheckLocalSendDataLimit(uint32_t& can_send_size, std::shared_ptr<IFrame>& send_frame);
    // check remote send data 
    void AddRemoteSendData(uint32_t size);
    bool CheckRemoteSendDataLimit(std::shared_ptr<IFrame>& send_frame);

    // check local bidirection stream limit
    void UpdateLocalBidirectionStreamLimit(uint64_t limit);
    bool CheckLocalBidirectionStreamLimit(uint64_t& stream_id, std::shared_ptr<IFrame>& send_frame);
    // check local unidirection stream limit
    void UpdateLocalUnidirectionStreamLimit(uint64_t limit);
    bool CheckLocalUnidirectionStreamLimit(uint64_t& stream_id, std::shared_ptr<IFrame>& send_frame);

    // check remote stream limit
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