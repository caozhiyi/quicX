#include "common/decode/decode.h"
#include "common/log/log.h"

#include "quic/connection/transport_param.h"
#include "quic/connection/type.h"

namespace quicx {
namespace quic {

namespace {
// RFC 9368 §3: version_information is a sequence of 32-bit fields:
//   Chosen Version (32)
//   Available Version (32) * N   // zero or more, in preference order
// The whole TP value length is multiple of 4 and >= 4.

// Encode version_information TP at |pos|. Returns new pos, or nullptr on error.
uint8_t* EncodeVersionInformation(
    uint8_t* pos, uint8_t* end, uint32_t chosen_version, const std::vector<uint32_t>& available_versions) {
    // TP id
    pos = common::EncodeVarint(pos, end, static_cast<uint32_t>(TransportParamType::kVersionInformation));
    if (pos == nullptr) return nullptr;
    // TP length = 4 * (1 + available_versions.size())
    const uint64_t tp_len = static_cast<uint64_t>(4) * (1 + available_versions.size());
    pos = common::EncodeVarint(pos, end, tp_len);
    if (pos == nullptr) return nullptr;
    // chosen version
    pos = common::FixedEncodeUint32(pos, end, chosen_version);
    if (pos == nullptr) return nullptr;
    // available versions
    for (uint32_t v : available_versions) {
        pos = common::FixedEncodeUint32(pos, end, v);
        if (pos == nullptr) return nullptr;
    }
    return pos;
}
}  // namespace

TransportParam::TransportParam():
    max_idle_timeout_(0),
    max_udp_payload_size_(0),
    initial_max_data_(0),
    initial_max_stream_data_bidi_local_(0),
    initial_max_stream_data_bidi_remote_(0),
    initial_max_stream_data_uni_(0),
    peer_initial_max_stream_data_bidi_remote_(0),
    peer_initial_max_stream_data_bidi_local_(0),
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
    // RFC 9000 §18: Transport parameters are mostly peer-set limits; adopt remote values directly.
    // Only max_idle_timeout uses min (RFC 9000 §10.1: "minimum of the values ... from both endpoints").
    max_idle_timeout_ = (max_idle_timeout_ == 0) ? tp.max_idle_timeout_
                      : (tp.max_idle_timeout_ == 0) ? max_idle_timeout_
                      : std::min(tp.max_idle_timeout_, max_idle_timeout_);
    max_udp_payload_size_ = tp.max_udp_payload_size_;
    initial_max_data_ = tp.initial_max_data_;
    // NOTE: initial_max_stream_data_bidi_local_ and initial_max_stream_data_bidi_remote_
    // are intentionally NOT overwritten. These fields store OUR local values:
    //   - bidi_local_: our receive window for locally-initiated bidi streams
    //   - bidi_remote_: our receive window for remotely-initiated bidi streams
    // Instead, store the peer's values in separate fields for send window usage.
    peer_initial_max_stream_data_bidi_remote_ = tp.initial_max_stream_data_bidi_remote_;
    peer_initial_max_stream_data_bidi_local_ = tp.initial_max_stream_data_bidi_local_;
    initial_max_stream_data_uni_ = tp.initial_max_stream_data_uni_;
    initial_max_streams_bidi_ = tp.initial_max_streams_bidi_;
    initial_max_streams_uni_ = tp.initial_max_streams_uni_;
    ack_delay_exponent_ = tp.ack_delay_exponent_;
    max_ack_delay_ = tp.max_ack_delay_;
    disable_active_migration_ = disable_active_migration_ || tp.disable_active_migration_;
    preferred_address_ = tp.preferred_address_;
    active_connection_id_limit_ = tp.active_connection_id_limit_;
    initial_source_connection_id_ = tp.initial_source_connection_id_;
    retry_source_connection_id_ = tp.retry_source_connection_id_;

    // RFC 9368: Record peer's version_information as-is (used for downgrade detection
    // by the upper layer). We intentionally overwrite our local value here because the
    // downgrade check must look at the *peer's* advertised lists.
    if (tp.has_version_information_) {
        has_version_information_ = true;
        chosen_version_ = tp.chosen_version_;
        available_versions_ = tp.available_versions_;
    }
    // NOTE: Do NOT notify listeners here. After Merge(), transport_param_ contains
    // remote values which are correct for SendFlowController but WRONG for RecvFlowController.
    // RecvFlowController needs LOCAL values (how much we allow the peer to send us).
    // The caller (OnTransportParams) is responsible for updating each controller appropriately.
    return true;
}

bool TransportParam::Encode(const common::BufferSpan& buffer, size_t& bytes_written) {
    if (!buffer.Valid()) {
        return false;
    }

    size_t buffer_size = buffer.GetLength();
    if (buffer_size < EncodeSize()) {
        return false;
    }

    uint8_t* pos = buffer.GetStart();
    uint8_t* end = buffer.GetEnd();

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

    // RFC 9368 version_information (TP 0x11)
    if (has_version_information_) {
        pos = EncodeVersionInformation(pos, end, chosen_version_, available_versions_);
        if (pos == nullptr) return false;
    }

    if (pos == nullptr || pos < buffer.GetStart() || pos > buffer.GetEnd()) {
        return false;
    }
    bytes_written = pos - buffer.GetStart();
    return true;
}

bool TransportParam::Decode(const common::BufferSpan& buffer) {
    uint64_t type = 0;
    uint8_t* pos = buffer.GetStart();
    uint8_t* end = buffer.GetEnd();
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
            case TransportParamType::kVersionInformation: {
                // RFC 9368 §3: Decode version_information TP (id 0x11).
                // Structure after the TP id (already consumed):
                //   varint(tp_len) + Chosen Version (32) + Available Version (32) * N
                uint64_t tp_len = 0;
                pos = common::DecodeVarint(pos, end, tp_len);
                if (pos == nullptr) {
                    common::LOG_ERROR("TransportParam: failed to decode version_information length");
                    return false;
                }
                // tp_len must be >= 4 (at least Chosen Version) and multiple of 4.
                if (tp_len < 4 || (tp_len % 4) != 0) {
                    common::LOG_ERROR("TransportParam: invalid version_information length %llu",
                        static_cast<unsigned long long>(tp_len));
                    return false;
                }
                uint8_t* value_end = pos + tp_len;
                if (value_end > end) {
                    common::LOG_ERROR("TransportParam: version_information length exceeds buffer");
                    return false;
                }
                uint32_t chosen = 0;
                pos = common::FixedDecodeUint32(pos, value_end, chosen);
                if (pos == nullptr) return false;
                std::vector<uint32_t> versions;
                const size_t remain_versions = static_cast<size_t>((tp_len - 4) / 4);
                versions.reserve(remain_versions);
                for (size_t i = 0; i < remain_versions; i++) {
                    uint32_t v = 0;
                    pos = common::FixedDecodeUint32(pos, value_end, v);
                    if (pos == nullptr) return false;
                    versions.push_back(v);
                }
                chosen_version_ = chosen;
                available_versions_ = std::move(versions);
                has_version_information_ = true;
                break;
            }
            default: {
                // RFC 9000 §18: "An endpoint MUST ignore transport parameters that it does not support."
                uint64_t param_len = 0;
                pos = common::DecodeVarint(pos, end, param_len);
                if (pos == nullptr || pos + param_len > end) {
                    common::LOG_ERROR("failed to skip unknown transport param. type:%d", type);
                    return false;
                }
                pos += param_len;  // skip unknown param value
                common::LOG_WARN("skipping unknown transport param. type:%d, len:%d", type, param_len);
                break;
            }
        }
    }
    return true;
}

uint32_t TransportParam::EncodeSize() {
    uint32_t size = 0;

    // Each uint param encodes as: varint(type) + varint(varint_len(value)) + varint(value)
    auto uint_param_size = [](uint32_t type, uint64_t value) -> uint32_t {
        uint16_t value_varint_len = common::GetEncodeVarintLength(value);
        return common::GetEncodeVarintLength(type)
             + common::GetEncodeVarintLength(value_varint_len)
             + value_varint_len;
    };

    // Each string param encodes as: varint(type) + varint(str.length()) + str.length()
    auto string_param_size = [](uint32_t type, const std::string& value) -> uint32_t {
        return common::GetEncodeVarintLength(type)
             + common::GetEncodeVarintLength(value.length())
             + static_cast<uint32_t>(value.length());
    };

    // Each bool param encodes as: varint(type) + varint(0) (zero-length per RFC 9000 §18)
    auto bool_param_size = [](uint32_t type) -> uint32_t {
        return common::GetEncodeVarintLength(type)
             + common::GetEncodeVarintLength(0);
    };

    if (!original_destination_connection_id_.empty()) {
        size += string_param_size(
            static_cast<uint32_t>(TransportParamType::kOriginalDestinationConnectionId),
            original_destination_connection_id_);
    }
    if (max_idle_timeout_) {
        size += uint_param_size(static_cast<uint32_t>(TransportParamType::kMaxIdleTimeout), max_idle_timeout_);
    }
    if (!stateless_reset_token_.empty()) {
        size += string_param_size(
            static_cast<uint32_t>(TransportParamType::kStatelessResetToken), stateless_reset_token_);
    }
    if (max_udp_payload_size_) {
        size += uint_param_size(static_cast<uint32_t>(TransportParamType::kMaxUdpPayloadSize), max_udp_payload_size_);
    }
    if (initial_max_data_) {
        size += uint_param_size(static_cast<uint32_t>(TransportParamType::kInitialMaxData), initial_max_data_);
    }
    if (initial_max_stream_data_bidi_local_) {
        size += uint_param_size(
            static_cast<uint32_t>(TransportParamType::kInitialMaxStreamDataBidiLocal),
            initial_max_stream_data_bidi_local_);
    }
    if (initial_max_stream_data_bidi_remote_) {
        size += uint_param_size(
            static_cast<uint32_t>(TransportParamType::kInitialMaxStreamDataBidiRemote),
            initial_max_stream_data_bidi_remote_);
    }
    if (initial_max_stream_data_uni_) {
        size += uint_param_size(
            static_cast<uint32_t>(TransportParamType::kInitialMaxStreamDataUni), initial_max_stream_data_uni_);
    }
    if (initial_max_streams_bidi_) {
        size += uint_param_size(
            static_cast<uint32_t>(TransportParamType::kInitialMaxStreamsBidi), initial_max_streams_bidi_);
    }
    if (initial_max_streams_uni_) {
        size += uint_param_size(
            static_cast<uint32_t>(TransportParamType::kInitialMaxStreamsUni), initial_max_streams_uni_);
    }
    if (ack_delay_exponent_) {
        size += uint_param_size(static_cast<uint32_t>(TransportParamType::kAckDelayExponent), ack_delay_exponent_);
    }
    if (max_ack_delay_) {
        size += uint_param_size(static_cast<uint32_t>(TransportParamType::kMaxAckDelay), max_ack_delay_);
    }
    if (disable_active_migration_) {
        size += bool_param_size(static_cast<uint32_t>(TransportParamType::kDisableActiveMigration));
    }
    if (!preferred_address_.empty()) {
        size += string_param_size(
            static_cast<uint32_t>(TransportParamType::kPreferredAddress), preferred_address_);
    }
    if (active_connection_id_limit_) {
        size += uint_param_size(
            static_cast<uint32_t>(TransportParamType::kActiveConnectionIdLimit), active_connection_id_limit_);
    }
    if (!initial_source_connection_id_.empty()) {
        size += string_param_size(
            static_cast<uint32_t>(TransportParamType::kInitialSourceConnectionId), initial_source_connection_id_);
    }
    if (!retry_source_connection_id_.empty()) {
        size += string_param_size(
            static_cast<uint32_t>(TransportParamType::kRetrySourceConnectionId), retry_source_connection_id_);
    }

    // RFC 9368 version_information: varint(type) + varint(tp_len) + 4 * (1 + N) bytes
    if (has_version_information_) {
        const uint32_t tp_len = static_cast<uint32_t>(4 * (1 + available_versions_.size()));
        size += common::GetEncodeVarintLength(static_cast<uint32_t>(TransportParamType::kVersionInformation))
             +  common::GetEncodeVarintLength(tp_len)
             +  tp_len;
    }

    return size;
}

uint8_t* TransportParam::EncodeUint(uint8_t* start, uint8_t* end, uint64_t value, uint32_t type) {
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
    // RFC 9000 §18: Zero-length parameters — presence indicates true.
    start = common::EncodeVarint(start, end, type);
    start = common::EncodeVarint(start, end, 0);  // zero-length value
    return start;
}

uint8_t* TransportParam::DecodeUint(uint8_t* start, uint8_t* end, uint64_t& value) {
    if (start == nullptr || end == nullptr) {
        return nullptr;
    }

    uint64_t length = 0;
    // read length
    start = common::DecodeVarint(start, end, length);
    if (start == nullptr) {
        return nullptr;
    }

    // Validate length against remaining buffer (RFC 9000 §18)
    uint8_t* value_end = start + length;
    if (value_end > end) {
        common::LOG_ERROR("TransportParam::DecodeUint: length exceeds buffer. length:%llu", length);
        return nullptr;
    }

    // read value within the length-delimited region
    uint64_t varint = 0;
    start = common::DecodeVarint(start, value_end, varint);
    if (start == nullptr) {
        return nullptr;
    }

    value = varint;
    // Skip to the end of the value region to handle any padding
    return value_end;
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

    uint64_t length = 0;
    // read length
    start = common::DecodeVarint(start, end, length);
    if (start == nullptr) {
        return nullptr;
    }

    // RFC 9000 §18: Zero-length parameter — presence indicates true.
    if (length == 0) {
        value = true;
        return start;
    }

    // For backward compatibility, also accept length=1 with a value byte
    uint64_t varint = 0;
    start = common::DecodeVarint(start, end, varint);
    if (start == nullptr) {
        return nullptr;
    }
    value = varint > 0;
    return start;
}

}  // namespace quic
}  // namespace quicx