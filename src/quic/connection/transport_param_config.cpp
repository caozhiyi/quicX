#include "common/util/time.h"
#include "quic/common/constants.h"
#include "common/timer/if_timer.h"
#include "quic/connection/transport_param_config.h"

namespace quicx {
namespace quic {


TransportParamConfig::TransportParamConfig():
    original_destination_connection_id_(),
    max_idle_timeout_(5 * common::TU_MILLISECOND),
    max_udp_payload_size_(__max_v4_packet_size),
    initial_max_data_(__max_v4_packet_size*10),
    initial_max_stream_data_bidi_local_(__max_v4_packet_size*10),
    initial_max_stream_data_bidi_remote_(__max_v4_packet_size*10),
    initial_max_stream_data_uni_(__max_v4_packet_size*10),
    initial_max_streams_bidi_(6),
    initial_max_streams_uni_(6),
    ack_delay_exponent_(5 * common::TU_MILLISECOND),
    max_ack_delay_(5 * common::TU_MILLISECOND),
    disable_active_migration_(false),
    preferred_address_(),
    active_connection_id_limit_(3),
    initial_source_connection_id_(),
    retry_source_connection_id_() {

}

TransportParamConfig::~TransportParamConfig() {

}

}
}