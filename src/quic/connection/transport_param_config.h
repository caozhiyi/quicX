#ifndef QUIC_CONNECTION_TRANSPORT_PARAM_CONFIG
#define QUIC_CONNECTION_TRANSPORT_PARAM_CONFIG

#include <string>
#include <cstdint>
#include "common/util/singleton.h"

namespace quicx {
namespace quic {

/*
 * transmission parameters local configuration parameters
 */
class TransportParamConfig:
    public common::Singleton<TransportParamConfig> {
public:
    TransportParamConfig();
    ~TransportParamConfig();

public:
    std::string original_destination_connection_id_;
    uint32_t    max_idle_timeout_;
    std::string stateless_reset_token_;
    uint32_t    max_udp_payload_size_;
    uint32_t    initial_max_data_;
    uint32_t    initial_max_stream_data_bidi_local_;
    uint32_t    initial_max_stream_data_bidi_remote_;
    uint32_t    initial_max_stream_data_uni_;
    uint32_t    initial_max_streams_bidi_;
    uint32_t    initial_max_streams_uni_;
    uint32_t    ack_delay_exponent_;
    uint32_t    max_ack_delay_;
    bool        disable_active_migration_;
    std::string preferred_address_;
    uint32_t    active_connection_id_limit_;
    std::string initial_source_connection_id_;
    std::string retry_source_connection_id_;
};

}
}

#endif