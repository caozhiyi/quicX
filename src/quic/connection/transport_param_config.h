#ifndef QUIC_CONNECTION_TRANSPORT_PARAM_CONFIG
#define QUIC_CONNECTION_TRANSPORT_PARAM_CONFIG

#include <string>
#include <cstdint>

namespace quicx {

struct TransportParamConfig {
    std::string _original_destination_connection_id;
    uint32_t    _max_idle_timeout;
    std::string _stateless_reset_token;
    uint32_t    _max_udp_payload_size;
    uint32_t    _initial_max_data;
    uint32_t    _initial_max_stream_data_bidi_local;
    uint32_t    _initial_max_stream_data_bidi_remote;
    uint32_t    _initial_max_stream_data_uni;
    uint32_t    _initial_max_streams_bidi;
    uint32_t    _initial_max_streams_uni;
    uint32_t    _ack_delay_exponent;
    uint32_t    _max_ack_delay;
    bool        _disable_active_migration;
    std::string _preferred_address;
    uint32_t    _active_connection_id_limit;
    std::string _initial_source_connection_id;
    std::string _retry_source_connection_id;
};

}

#endif