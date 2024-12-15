#include "common/log/log.h"
#include "common/decode/decode.h"
#include "common/buffer/if_buffer.h"
#include "quic/connection/transport_param.h"
#include "quic/connection/transport_param_type.h"
#include "quic/connection/transport_param_config.h"

namespace quicx {
namespace quic {

TransportParam::TransportParam():
    max_idle_timeout_(0),
    max_udp_payload_size_(0),
    initial_max_data_(0),
    initial_max_stream_data_bidi_local_(0),
    initial_max_stream_data_bidi_remote_(0),
    initial_max_stream_data_uni_(0),
    initial_max_streams_bidi_(0),
    initial_max_streams_uni_(0),
    ack_delay_exponent_(0),
    max_ack_delay_(0),
    disable_active_migration_(false),
    active_connection_id_limit_(0) {

}

TransportParam::~TransportParam() {

}

void TransportParam::Init(TransportParamConfig& conf) {
    original_destination_connection_id_ = conf.original_destination_connection_id_;
    max_idle_timeout_ = conf.max_idle_timeout_;
    stateless_reset_token_ = conf.stateless_reset_token_;
    max_udp_payload_size_ = conf.max_udp_payload_size_;
    initial_max_data_ = conf.initial_max_data_;
    initial_max_stream_data_bidi_local_ = conf.initial_max_stream_data_bidi_local_;
    initial_max_stream_data_bidi_remote_ = conf.initial_max_stream_data_bidi_remote_;
    initial_max_stream_data_uni_ = conf.initial_max_stream_data_uni_;
    initial_max_streams_bidi_ = conf.initial_max_streams_bidi_;
    initial_max_streams_uni_ = conf.initial_max_streams_uni_;
    ack_delay_exponent_ = conf.ack_delay_exponent_;
    max_ack_delay_ = conf.max_ack_delay_;
    disable_active_migration_ = conf.disable_active_migration_;
    preferred_address_ = conf.preferred_address_;
    active_connection_id_limit_ = conf.active_connection_id_limit_;
    initial_source_connection_id_ = conf.initial_source_connection_id_;
    retry_source_connection_id_ = conf.retry_source_connection_id_;
}

 bool TransportParam::Merge(const TransportParam& tp) {
     // the max idle time takes the minimum value
     if (tp.max_idle_timeout_ < max_idle_timeout_) {
         max_idle_timeout_ = tp.max_idle_timeout_;
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
    if (!original_destination_connection_id_.empty()) {
        pos = EncodeString(pos, end, original_destination_connection_id_, TP_ORIGINAL_DESTINATION_CONNECTION_ID);
    }
    
    if (max_idle_timeout_) {
        pos = EncodeUint(pos, end, max_idle_timeout_, TP_MAX_IDLE_TIMEOUT);
    }
    
    if (!stateless_reset_token_.empty()) {
        pos = EncodeString(pos, end, stateless_reset_token_, TP_STATELESS_RESET_TOKEN);
    }
    
    if (max_udp_payload_size_) {
        pos = EncodeUint(pos, end, max_udp_payload_size_, TP_MAX_UDP_PAYLOAD_SIZE);
    }
    
    if (initial_max_data_) {
        pos = EncodeUint(pos, end, initial_max_data_, TP_INITIAL_MAX_DATA);
    }

    if (initial_max_stream_data_bidi_local_) {
        pos = EncodeUint(pos, end, initial_max_stream_data_bidi_local_, TP_INITIAL_MAX_STREAM_DATA_BIDI_LOCAL);
    }
    
    if (initial_max_stream_data_bidi_remote_) {
        pos = EncodeUint(pos, end, initial_max_stream_data_bidi_remote_, TP_INITIAL_MAX_STREAM_DATA_BIDI_REMOTE);
    }

    if (initial_max_stream_data_uni_) {
        pos = EncodeUint(pos, end, initial_max_stream_data_uni_, TP_INITIAL_MAX_STREAM_DATA_UNI);
    }

    if (initial_max_streams_bidi_) {
        pos = EncodeUint(pos, end, initial_max_streams_bidi_, TP_INITIAL_MAX_STREAMS_BIDI);
    }

    if (initial_max_streams_uni_) {
        pos = EncodeUint(pos, end, initial_max_streams_uni_, TP_INITIAL_MAX_STREAMS_UNI);
    }
    
    if (ack_delay_exponent_) {
        pos = EncodeUint(pos, end, ack_delay_exponent_, TP_ACK_DELAY_EXPONENT);
    }

    if (max_ack_delay_) {
        pos = EncodeUint(pos, end, max_ack_delay_, TP_MAX_ACK_DELAY);
    }

    if (disable_active_migration_) {
        pos = EncodeBool(pos, end, disable_active_migration_, TP_DISABLE_ACTIVE_MIGRATION);
    }

    if (!preferred_address_.empty()) {
        pos = EncodeString(pos, end, preferred_address_, TP_PREFERRED_ADDRESS);
    }

    if (active_connection_id_limit_) {
        pos = EncodeUint(pos, end, active_connection_id_limit_, TP_ACTIVE_CONNECTION_ID_LIMIT);
    }

    if (!initial_source_connection_id_.empty()) {
        pos = EncodeString(pos, end, initial_source_connection_id_, TP_INITIAL_SOURCE_CONNECTION_ID);
    }

    if (!retry_source_connection_id_.empty()) {
        pos = EncodeString(pos, end, retry_source_connection_id_, TP_RETRY_SOURCE_CONNECTION_ID);
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
            pos = DecodeString(pos, end, original_destination_connection_id_);
            break;
        case TP_MAX_IDLE_TIMEOUT:
            pos = DecodeUint(pos, end, max_idle_timeout_);
            break;
        case TP_STATELESS_RESET_TOKEN:
            pos = DecodeString(pos, end, stateless_reset_token_);
            break;
        case TP_MAX_UDP_PAYLOAD_SIZE:
            pos = DecodeUint(pos, end, max_udp_payload_size_);
            break;
        case TP_INITIAL_MAX_DATA:
            pos = DecodeUint(pos, end, initial_max_data_);
            break;
        case TP_INITIAL_MAX_STREAM_DATA_BIDI_LOCAL:
            pos = DecodeUint(pos, end, initial_max_stream_data_bidi_local_);
            break;
        case TP_INITIAL_MAX_STREAM_DATA_BIDI_REMOTE:
            pos = DecodeUint(pos, end, initial_max_stream_data_bidi_remote_);
            break;
        case TP_INITIAL_MAX_STREAM_DATA_UNI:
            pos = DecodeUint(pos, end, initial_max_stream_data_uni_);
            break;
        case TP_INITIAL_MAX_STREAMS_BIDI:
            pos = DecodeUint(pos, end, initial_max_streams_bidi_);
            break;
        case TP_INITIAL_MAX_STREAMS_UNI:
            pos = DecodeUint(pos, end, initial_max_streams_uni_);
            break;
        case TP_ACK_DELAY_EXPONENT:
            pos = DecodeUint(pos, end, ack_delay_exponent_);
            break;
        case TP_MAX_ACK_DELAY:
            pos = DecodeUint(pos, end, max_ack_delay_);
            break;
        case TP_DISABLE_ACTIVE_MIGRATION:
            pos = DecodeBool(pos, end, disable_active_migration_);
            break;
        case TP_PREFERRED_ADDRESS:
            pos = DecodeString(pos, end, preferred_address_);
            break;
        case TP_ACTIVE_CONNECTION_ID_LIMIT:
            pos = DecodeUint(pos, end, active_connection_id_limit_);
            break;
        case TP_INITIAL_SOURCE_CONNECTION_ID:
            pos = DecodeString(pos, end, initial_source_connection_id_);
            break;
        case TP_RETRY_SOURCE_CONNECTION_ID:
            pos = DecodeString(pos, end, retry_source_connection_id_);
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