#include "common/log/log.h"
#include "common/decode/decode.h"
#include "common/buffer/buffer_interface.h"
#include "quic/connection/transport_param.h"
#include "quic/connection/transport_param_type.h"
#include "quic/connection/transport_param_config.h"

namespace quicx {
namespace quic {

TransportParam::TransportParam():
    _max_idle_timeout(0),
    _max_udp_payload_size(0),
    _initial_max_data(0),
    _initial_max_stream_data_bidi_local(0),
    _initial_max_stream_data_bidi_remote(0),
    _initial_max_stream_data_uni(0),
    _initial_max_streams_bidi(0),
    _initial_max_streams_uni(0),
    _ack_delay_exponent(0),
    _max_ack_delay(0),
    _disable_active_migration(false),
    _active_connection_id_limit(0) {

}

TransportParam::~TransportParam() {

}

void TransportParam::Init(TransportParamConfig& conf) {
    _original_destination_connection_id = conf._original_destination_connection_id;
    _max_idle_timeout = conf._max_idle_timeout;
    _stateless_reset_token = conf._stateless_reset_token;
    _max_udp_payload_size = conf._max_udp_payload_size;
    _initial_max_data = conf._initial_max_data;
    _initial_max_stream_data_bidi_local = conf._initial_max_stream_data_bidi_local;
    _initial_max_stream_data_bidi_remote = conf._initial_max_stream_data_bidi_remote;
    _initial_max_stream_data_uni = conf._initial_max_stream_data_uni;
    _initial_max_streams_bidi = conf._initial_max_streams_bidi;
    _initial_max_streams_uni = conf._initial_max_streams_uni;
    _ack_delay_exponent = conf._ack_delay_exponent;
    _max_ack_delay = conf._max_ack_delay;
    _disable_active_migration = conf._disable_active_migration;
    _preferred_address = conf._preferred_address;
    _active_connection_id_limit = conf._active_connection_id_limit;
    _initial_source_connection_id = conf._initial_source_connection_id;
    _retry_source_connection_id = conf._retry_source_connection_id;
}

 bool TransportParam::Merge(const TransportParam& tp) {
     // the max idle time takes the minimum value
     if (tp._max_idle_timeout < _max_idle_timeout) {
         _max_idle_timeout = tp._max_idle_timeout;
     }
     
     return true;
 }

bool TransportParam::Encode(std::shared_ptr<common::IBufferWrite> buffer) {
    if (buffer->GetFreeLength() < EncodeSize()) {
        return false;
    }
    
    auto span = buffer->GetWriteSpan();
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();
    if (!_original_destination_connection_id.empty()) {
        pos = EncodeString(pos, end, _original_destination_connection_id, TP_ORIGINAL_DESTINATION_CONNECTION_ID);
    }
    
    if (_max_idle_timeout) {
        pos = EncodeUint(pos, end, _max_idle_timeout, TP_MAX_IDLE_TIMEOUT);
    }
    
    if (!_stateless_reset_token.empty()) {
        pos = EncodeString(pos, end, _stateless_reset_token, TP_STATELESS_RESET_TOKEN);
    }
    
    if (_max_udp_payload_size) {
        pos = EncodeUint(pos, end, _max_udp_payload_size, TP_MAX_UDP_PAYLOAD_SIZE);
    }
    
    if (_initial_max_data) {
        pos = EncodeUint(pos, end, _initial_max_data, TP_INITIAL_MAX_DATA);
    }

    if (_initial_max_stream_data_bidi_local) {
        pos = EncodeUint(pos, end, _initial_max_stream_data_bidi_local, TP_INITIAL_MAX_STREAM_DATA_BIDI_LOCAL);
    }
    
    if (_initial_max_stream_data_bidi_remote) {
        pos = EncodeUint(pos, end, _initial_max_stream_data_bidi_remote, TP_INITIAL_MAX_STREAM_DATA_BIDI_REMOTE);
    }

    if (_initial_max_stream_data_uni) {
        pos = EncodeUint(pos, end, _initial_max_stream_data_uni, TP_INITIAL_MAX_STREAM_DATA_UNI);
    }

    if (_initial_max_streams_bidi) {
        pos = EncodeUint(pos, end, _initial_max_streams_bidi, TP_INITIAL_MAX_STREAMS_BIDI);
    }

    if (_initial_max_streams_uni) {
        pos = EncodeUint(pos, end, _initial_max_streams_uni, TP_INITIAL_MAX_STREAMS_UNI);
    }
    
    if (_ack_delay_exponent) {
        pos = EncodeUint(pos, end, _ack_delay_exponent, TP_ACK_DELAY_EXPONENT);
    }

    if (_max_ack_delay) {
        pos = EncodeUint(pos, end, _max_ack_delay, TP_MAX_ACK_DELAY);
    }

    if (_disable_active_migration) {
        pos = EncodeBool(pos, end, _disable_active_migration, TP_DISABLE_ACTIVE_MIGRATION);
    }

    if (!_preferred_address.empty()) {
        pos = EncodeString(pos, end, _preferred_address, TP_PREFERRED_ADDRESS);
    }

    if (_active_connection_id_limit) {
        pos = EncodeUint(pos, end, _active_connection_id_limit, TP_ACTIVE_CONNECTION_ID_LIMIT);
    }

    if (!_initial_source_connection_id.empty()) {
        pos = EncodeString(pos, end, _initial_source_connection_id, TP_INITIAL_SOURCE_CONNECTION_ID);
    }

    if (!_retry_source_connection_id.empty()) {
        pos = EncodeString(pos, end, _retry_source_connection_id, TP_RETRY_SOURCE_CONNECTION_ID);
    }

    buffer->MoveWritePt(pos - span.GetStart());
    return true;
}

bool TransportParam::Decode(std::shared_ptr<common::IBufferRead> buffer) {
    uint64_t type = 0;
    auto span = buffer->GetReadSpan();
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();
    while (pos < end) {
        pos = common::DecodeVarint(pos, end, type);
        switch(type) {
        case TP_ORIGINAL_DESTINATION_CONNECTION_ID:
            pos = DecodeString(pos, end, _original_destination_connection_id);
            break;
        case TP_MAX_IDLE_TIMEOUT:
            pos = DecodeUint(pos, end, _max_idle_timeout);
            break;
        case TP_STATELESS_RESET_TOKEN:
            pos = DecodeString(pos, end, _stateless_reset_token);
            break;
        case TP_MAX_UDP_PAYLOAD_SIZE:
            pos = DecodeUint(pos, end, _max_udp_payload_size);
            break;
        case TP_INITIAL_MAX_DATA:
            pos = DecodeUint(pos, end, _initial_max_data);
            break;
        case TP_INITIAL_MAX_STREAM_DATA_BIDI_LOCAL:
            pos = DecodeUint(pos, end, _initial_max_stream_data_bidi_local);
            break;
        case TP_INITIAL_MAX_STREAM_DATA_BIDI_REMOTE:
            pos = DecodeUint(pos, end, _initial_max_stream_data_bidi_remote);
            break;
        case TP_INITIAL_MAX_STREAM_DATA_UNI:
            pos = DecodeUint(pos, end, _initial_max_stream_data_uni);
            break;
        case TP_INITIAL_MAX_STREAMS_BIDI:
            pos = DecodeUint(pos, end, _initial_max_streams_bidi);
            break;
        case TP_INITIAL_MAX_STREAMS_UNI:
            pos = DecodeUint(pos, end, _initial_max_streams_uni);
            break;
        case TP_ACK_DELAY_EXPONENT:
            pos = DecodeUint(pos, end, _ack_delay_exponent);
            break;
        case TP_MAX_ACK_DELAY:
            pos = DecodeUint(pos, end, _max_ack_delay);
            break;
        case TP_DISABLE_ACTIVE_MIGRATION:
            pos = DecodeBool(pos, end, _disable_active_migration);
            break;
        case TP_PREFERRED_ADDRESS:
            pos = DecodeString(pos, end, _preferred_address);
            break;
        case TP_ACTIVE_CONNECTION_ID_LIMIT:
            pos = DecodeUint(pos, end, _active_connection_id_limit);
            break;
        case TP_INITIAL_SOURCE_CONNECTION_ID:
            pos = DecodeString(pos, end, _initial_source_connection_id);
            break;
        case TP_RETRY_SOURCE_CONNECTION_ID:
            pos = DecodeString(pos, end, _retry_source_connection_id);
            break;
        default:
            common::LOG_ERROR("unsupport stransport param. type:%d", type);
            return false;
        }
    }
    buffer->MoveReadPt(pos - end);
    return true;
}

uint32_t TransportParam::EncodeSize() {
    return sizeof(TransportParam);
}

uint8_t* TransportParam::EncodeUint(uint8_t* start, uint8_t* end, uint32_t value, uint32_t type) {
    start = common::EncodeVarint(start, type);
    start = common::EncodeVarint(start, common::GetEncodeVarintLength(value));
    start = common::EncodeVarint(start, value);
    return start;
}

uint8_t* TransportParam::EncodeString(uint8_t* start, uint8_t* end, const std::string& value, uint32_t type) {
    start = common::EncodeVarint(start, type);
    start = common::EncodeVarint(start, value.length());
    start = common::EncodeBytes(start, end, (uint8_t*)value.c_str(), value.length());
    return start;
}

uint8_t* TransportParam::EncodeBool(uint8_t* start, uint8_t* end, bool value, uint32_t type) {
    start = common::EncodeVarint(start, type);
    start = common::EncodeVarint(start, 1);
    start = common::EncodeVarint(start, value ? 1 : 0);
    return start;
}

uint8_t* TransportParam::DecodeUint(uint8_t* start, uint8_t* end, uint32_t& value) {
    uint64_t varint = 0;
    // read length
    start = common::DecodeVarint(start, end, varint);
    // read value
    start = common::DecodeVarint(start, end, varint);
    value = varint;
    return start;
}

uint8_t* TransportParam::DecodeString(uint8_t* start, uint8_t* end, std::string& value) {
    uint64_t length = 0;
    // read length
    start = common::DecodeVarint(start, end, length);
    // read value
    uint8_t* ptr;
    start = common::DecodeBytesNoCopy(start, end, ptr, length);
    value = std::move(std::string((const char*)ptr, length));
    return start;
}

uint8_t* TransportParam::DecodeBool(uint8_t* start, uint8_t* end, bool& value) {
    uint64_t varint = 0;
    // read length
    start = common::DecodeVarint(start, end, varint);
    // read value
    start = common::DecodeVarint(start, end, varint);
    value = varint > 0;
    return start;
}

}
}