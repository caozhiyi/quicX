#include "quic/common/constants.h"
#include "common/timer/timer_interface.h"
#include "quic/connection/transport_param_config.h"

namespace quicx {


TransportParamConfig::TransportParamConfig():
    _original_destination_connection_id(),
    _max_idle_timeout(5 * TU_MILLISECOND),
    _max_udp_payload_size(__max_v4_packet_size),
    _initial_max_data(__max_v4_packet_size),
    _initial_max_stream_data_bidi_local(__max_v4_packet_size*10),
    _initial_max_stream_data_bidi_remote(__max_v4_packet_size*10),
    _initial_max_stream_data_uni(__max_v4_packet_size*10),
    _initial_max_streams_bidi(6),
    _initial_max_streams_uni(6),
    _ack_delay_exponent(5 * TU_MILLISECOND),
    _max_ack_delay(5 * TU_MILLISECOND),
    _disable_active_migration(false),
    _preferred_address(),
    _active_connection_id_limit(3),
    _initial_source_connection_id(),
    _retry_source_connection_id() {

}

TransportParamConfig::~TransportParamConfig() {

}

}
