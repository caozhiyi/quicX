#include "quic/connection/transport_param.h"
#include "common/decode/decode.h"
#include "common/log/log.h"
#include "quic/connection/type.h"

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
    ack_delay_exponent_(3),
    max_ack_delay_(25),
    disable_active_migration_(false),
    active_connection_id_limit_(0) {}

TransportParam::~TransportParam() {}

void TransportParam::AddTransportParamListener(std::function<void(const TransportParam&)> listener) {
    transport_param_listeners_.push_back(listener);
}

void TransportParam::Init(const QuicTransportParams& conf) {
    original_destination_connection_id_ = conf.original_destination_connection_id_;
    max_idle_timeout_ = conf.max_idle_timeout_ms_;
    stateless_reset_token_ = conf.stateless_reset_token_;
    max_udp_payload_size_ = conf.max_udp_payload_size_;
    initial_max_data_ = conf.initial_max_data_;
    initial_max_stream_data_bidi_local_ = conf.initial_max_stream_data_bidi_local_;
    initial_max_stream_data_bidi_remote_ = conf.initial_max_stream_data_bidi_remote_;
    initial_max_stream_data_uni_ = conf.initial_max_stream_data_uni_;
    initial_max_streams_bidi_ = conf.initial_max_streams_bidi_;
    initial_max_streams_uni_ = conf.initial_max_streams_uni_;
    ack_delay_exponent_ = conf.ack_delay_exponent_ms_;
    max_ack_delay_ = conf.max_ack_delay_ms_;
    disable_active_migration_ = conf.disable_active_migration_;
    preferred_address_ = conf.preferred_address_;
    active_connection_id_limit_ = conf.active_connection_id_limit_;
    initial_source_connection_id_ = conf.initial_source_connection_id_;
    retry_source_connection_id_ = conf.retry_source_connection_id_;
    for (auto& listener : transport_param_listeners_) {
        listener(*this);
    }
}

bool TransportParam::Merge(const TransportParam& tp) {
    // merge transport param
    max_idle_timeout_ = std::min(tp.max_idle_timeout_, max_idle_timeout_);
    max_udp_payload_size_ = std::min(tp.max_udp_payload_size_, max_udp_payload_size_);
    initial_max_data_ = std::min(tp.initial_max_data_, initial_max_data_);
    initial_max_stream_data_bidi_local_ =
        std::min(tp.initial_max_stream_data_bidi_local_, initial_max_stream_data_bidi_local_);
    initial_max_stream_data_bidi_remote_ =
        std::min(tp.initial_max_stream_data_bidi_remote_, initial_max_stream_data_bidi_remote_);
    initial_max_stream_data_uni_ = std::min(tp.initial_max_stream_data_uni_, initial_max_stream_data_uni_);
    initial_max_streams_bidi_ = std::min(tp.initial_max_streams_bidi_, initial_max_streams_bidi_);
    initial_max_streams_uni_ = std::min(tp.initial_max_streams_uni_, initial_max_streams_uni_);
    ack_delay_exponent_ = std::min(tp.ack_delay_exponent_, ack_delay_exponent_);
    max_ack_delay_ = std::min(tp.max_ack_delay_, max_ack_delay_);
    disable_active_migration_ = disable_active_migration_ || tp.disable_active_migration_;
    preferred_address_ = tp.preferred_address_;
    active_connection_id_limit_ = std::min(tp.active_connection_id_limit_, active_connection_id_limit_);
    initial_source_connection_id_ = tp.initial_source_connection_id_;
    retry_source_connection_id_ = tp.retry_source_connection_id_;
    for (auto& listener : transport_param_listeners_) {
        listener(*this);
    }
    return true;
}

bool TransportParam::Encode(common::BufferWriteView& buffer) {
    if (buffer.GetFreeLength() < EncodeSize()) {
        return false;
    }

    auto span = buffer.GetWritableSpan();
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();
    if (!original_destination_connection_id_.empty()) {
        pos = EncodeString(pos, end, original_destination_connection_id_,
            static_cast<uint32_t>(TransportParamType::kOriginalDestinationConnectionId));
        if (pos == nullptr) return false;
    }

    if (max_idle_timeout_) {
        pos = EncodeUint(pos, end, max_idle_timeout_, static_cast<uint32_t>(TransportParamType::kMaxIdleTimeout));
        if (pos == nullptr) return false;
    }

    if (!stateless_reset_token_.empty()) {
        pos = EncodeString(
            pos, end, stateless_reset_token_, static_cast<uint32_t>(TransportParamType::kStatelessResetToken));
        if (pos == nullptr) return false;
    }

    if (max_udp_payload_size_) {
        pos =
            EncodeUint(pos, end, max_udp_payload_size_, static_cast<uint32_t>(TransportParamType::kMaxUdpPayloadSize));
        if (pos == nullptr) return false;
    }

    if (initial_max_data_) {
        pos = EncodeUint(pos, end, initial_max_data_, static_cast<uint32_t>(TransportParamType::kInitialMaxData));
        if (pos == nullptr) return false;
    }

    if (initial_max_stream_data_bidi_local_) {
        pos = EncodeUint(pos, end, initial_max_stream_data_bidi_local_,
            static_cast<uint32_t>(TransportParamType::kInitialMaxStreamDataBidiLocal));
        if (pos == nullptr) return false;
    }

    if (initial_max_stream_data_bidi_remote_) {
        pos = EncodeUint(pos, end, initial_max_stream_data_bidi_remote_,
            static_cast<uint32_t>(TransportParamType::kInitialMaxStreamDataBidiRemote));
        if (pos == nullptr) return false;
    }

    if (initial_max_stream_data_uni_) {
        pos = EncodeUint(pos, end, initial_max_stream_data_uni_,
            static_cast<uint32_t>(TransportParamType::kInitialMaxStreamDataUni));
        if (pos == nullptr) return false;
    }

    if (initial_max_streams_bidi_) {
        pos = EncodeUint(
            pos, end, initial_max_streams_bidi_, static_cast<uint32_t>(TransportParamType::kInitialMaxStreamsBidi));
        if (pos == nullptr) return false;
    }

    if (initial_max_streams_uni_) {
        pos = EncodeUint(
            pos, end, initial_max_streams_uni_, static_cast<uint32_t>(TransportParamType::kInitialMaxStreamsUni));
        if (pos == nullptr) return false;
    }

    if (ack_delay_exponent_) {
        pos = EncodeUint(pos, end, ack_delay_exponent_, static_cast<uint32_t>(TransportParamType::kAckDelayExponent));
        if (pos == nullptr) return false;
    }

    if (max_ack_delay_) {
        pos = EncodeUint(pos, end, max_ack_delay_, static_cast<uint32_t>(TransportParamType::kMaxAckDelay));
        if (pos == nullptr) return false;
    }

    if (disable_active_migration_) {
        pos = EncodeBool(
            pos, end, disable_active_migration_, static_cast<uint32_t>(TransportParamType::kDisableActiveMigration));
        if (pos == nullptr) return false;
    }

    if (!preferred_address_.empty()) {
        pos = EncodeString(pos, end, preferred_address_, static_cast<uint32_t>(TransportParamType::kPreferredAddress));
        if (pos == nullptr) return false;
    }

    if (active_connection_id_limit_) {
        pos = EncodeUint(
            pos, end, active_connection_id_limit_, static_cast<uint32_t>(TransportParamType::kActiveConnectionIdLimit));
        if (pos == nullptr) return false;
    }

    if (!initial_source_connection_id_.empty()) {
        pos = EncodeString(pos, end, initial_source_connection_id_,
            static_cast<uint32_t>(TransportParamType::kInitialSourceConnectionId));
        if (pos == nullptr) return false;
    }

    if (!retry_source_connection_id_.empty()) {
        pos = EncodeString(
            pos, end, retry_source_connection_id_, static_cast<uint32_t>(TransportParamType::kRetrySourceConnectionId));
        if (pos == nullptr) return false;
    }

    if (pos == nullptr || pos < span.GetStart() || pos > span.GetEnd()) {
        return false;
    }
    buffer.MoveWritePt(pos - span.GetStart());
    return true;
}

bool TransportParam::Decode(common::BufferReadView& buffer) {
    uint64_t type = 0;
    auto span = buffer.GetReadableSpan();
    uint8_t* pos = span.GetStart();
    uint8_t* end = span.GetEnd();
    while (pos != nullptr && pos < end) {
        pos = common::DecodeVarint(pos, end, type);
        if (pos == nullptr) {
            return false;
        }
        switch (static_cast<TransportParamType>(type)) {
            case TransportParamType::kOriginalDestinationConnectionId:
                pos = DecodeString(pos, end, original_destination_connection_id_);
                if (pos == nullptr) {
                    return false;
                }
                break;
            case TransportParamType::kMaxIdleTimeout:
                pos = DecodeUint(pos, end, max_idle_timeout_);
                if (pos == nullptr) return false;
                break;
            case TransportParamType::kStatelessResetToken:
                pos = DecodeString(pos, end, stateless_reset_token_);
                if (pos == nullptr) return false;
                break;
            case TransportParamType::kMaxUdpPayloadSize:
                pos = DecodeUint(pos, end, max_udp_payload_size_);
                if (pos == nullptr) return false;
                break;
            case TransportParamType::kInitialMaxData:
                pos = DecodeUint(pos, end, initial_max_data_);
                if (pos == nullptr) return false;
                break;
            case TransportParamType::kInitialMaxStreamDataBidiLocal:
                pos = DecodeUint(pos, end, initial_max_stream_data_bidi_local_);
                if (pos == nullptr) return false;
                break;
            case TransportParamType::kInitialMaxStreamDataBidiRemote:
                pos = DecodeUint(pos, end, initial_max_stream_data_bidi_remote_);
                if (pos == nullptr) return false;
                break;
            case TransportParamType::kInitialMaxStreamDataUni:
                pos = DecodeUint(pos, end, initial_max_stream_data_uni_);
                if (pos == nullptr) return false;
                break;
            case TransportParamType::kInitialMaxStreamsBidi:
                pos = DecodeUint(pos, end, initial_max_streams_bidi_);
                if (pos == nullptr) return false;
                break;
            case TransportParamType::kInitialMaxStreamsUni:
                pos = DecodeUint(pos, end, initial_max_streams_uni_);
                if (pos == nullptr) return false;
                break;
            case TransportParamType::kAckDelayExponent:
                pos = DecodeUint(pos, end, ack_delay_exponent_);
                if (pos == nullptr) return false;
                break;
            case TransportParamType::kMaxAckDelay:
                pos = DecodeUint(pos, end, max_ack_delay_);
                if (pos == nullptr) return false;
                break;
            case TransportParamType::kDisableActiveMigration:
                pos = DecodeBool(pos, end, disable_active_migration_);
                if (pos == nullptr) return false;
                break;
            case TransportParamType::kPreferredAddress:
                pos = DecodeString(pos, end, preferred_address_);
                if (pos == nullptr) return false;
                break;
            case TransportParamType::kActiveConnectionIdLimit:
                pos = DecodeUint(pos, end, active_connection_id_limit_);
                if (pos == nullptr) return false;
                break;
            case TransportParamType::kInitialSourceConnectionId:
                pos = DecodeString(pos, end, initial_source_connection_id_);
                if (pos == nullptr) return false;
                break;
            case TransportParamType::kRetrySourceConnectionId:
                pos = DecodeString(pos, end, retry_source_connection_id_);
                if (pos == nullptr) return false;
                break;
            default:
                common::LOG_ERROR("unsupport stransport param. type:%d", type);
                return false;
        }
    }
    buffer.MoveReadPt(pos - span.GetStart());
    return true;
}

uint32_t TransportParam::EncodeSize() {
    return sizeof(TransportParam);
}

uint8_t* TransportParam::EncodeUint(uint8_t* start, uint8_t* end, uint32_t value, uint32_t type) {
    start = common::EncodeVarint(start, end, type);
    start = common::EncodeVarint(start, end, common::GetEncodeVarintLength(value));
    start = common::EncodeVarint(start, end, value);
    return start;
}

uint8_t* TransportParam::EncodeString(uint8_t* start, uint8_t* end, const std::string& value, uint32_t type) {
    start = common::EncodeVarint(start, end, type);
    start = common::EncodeVarint(start, end, value.length());
    start = common::EncodeBytes(start, end, (uint8_t*)value.c_str(), value.length());
    return start;
}

uint8_t* TransportParam::EncodeBool(uint8_t* start, uint8_t* end, bool value, uint32_t type) {
    start = common::EncodeVarint(start, end, type);
    start = common::EncodeVarint(start, end, 1);
    start = common::EncodeVarint(start, end, value ? 1 : 0);
    return start;
}

uint8_t* TransportParam::DecodeUint(uint8_t* start, uint8_t* end, uint32_t& value) {
    if (start == nullptr || end == nullptr) {
        return nullptr;
    }

    uint64_t varint = 0;
    // read length
    start = common::DecodeVarint(start, end, varint);
    if (start == nullptr) {
        return nullptr;
    }

    // read value
    start = common::DecodeVarint(start, end, varint);
    if (start == nullptr) {
        return nullptr;
    }

    value = varint;
    return start;
}

uint8_t* TransportParam::DecodeString(uint8_t* start, uint8_t* end, std::string& value) {
    if (start == nullptr || end == nullptr) {
        return nullptr;
    }

    uint64_t length = 0;
    // read length
    start = common::DecodeVarint(start, end, length);
    if (start == nullptr) {
        return nullptr;
    }

    // read value
    uint8_t* ptr = nullptr;
    start = common::DecodeBytesNoCopy(start, end, ptr, length);
    if (start == nullptr || ptr == nullptr) {
        return nullptr;
    }

    value = std::move(std::string((const char*)ptr, length));
    return start;
}

uint8_t* TransportParam::DecodeBool(uint8_t* start, uint8_t* end, bool& value) {
    if (start == nullptr || end == nullptr) {
        return nullptr;
    }

    uint64_t varint = 0;
    // read length
    start = common::DecodeVarint(start, end, varint);
    if (start == nullptr) {
        return nullptr;
    }

    // read value
    start = common::DecodeVarint(start, end, varint);
    if (start == nullptr) {
        return nullptr;
    }

    value = varint > 0;
    return start;
}

}  // namespace quic
}  // namespace quicx