#ifndef QUIC_CONNECTION_CONTROLER_FLOW_CONTROL
#define QUIC_CONNECTION_CONTROLER_FLOW_CONTROL

#include "quic/frame/frame_interface.h"
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
    void InitConfig(TransportParam& tp);

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
    uint64_t _local_send_max_data_limit;
    uint64_t _local_send_data_size;
    uint64_t _remote_send_max_data_limit;
    uint64_t _remote_send_data_size;
    // streams flow control
    uint64_t _local_max_bidirectional_stream_id;
    uint64_t _local_bidirectional_stream_limit;
    uint64_t _local_max_unidirectional_stream_id;
    uint64_t _local_unidirectional_stream_limit;
    
    uint64_t _remote_max_bidirectional_stream_id;
    uint64_t _remote_bidirectional_stream_limit;
    uint64_t _remote_max_unidirectional_stream_id;
    uint64_t _remote_unidirectional_stream_limit;

    StreamIDGenerator _id_generator;
};

}
}

#endif