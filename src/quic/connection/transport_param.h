#ifndef QUIC_CONNECTION_TRANSPORT_PARAM
#define QUIC_CONNECTION_TRANSPORT_PARAM

#include <string>
#include <memory>
#include <cstdint>

namespace quicx {

class IBufferReadOnly;
class IBufferWriteOnly;
class TransportParamConfig;
class TransportParam {
public:
    TransportParam();
    ~TransportParam();

    void Init(TransportParamConfig& conf);

    bool Encode(std::shared_ptr<IBufferWriteOnly> buffer);
    bool Decode(std::shared_ptr<IBufferReadOnly> buffer);
    uint32_t EncodeSize();


    const std::string& GetOriginalDestinationConnectionId() { return _original_destination_connection_id; }
    uint32_t GetMaxIdleTimeout() { return _max_idle_timeout; }
    const std::string& GetStatelessResetToken() { return _stateless_reset_token; }
    uint32_t GetmaxUdpPayloadSize() { return _max_udp_payload_size; }
    uint32_t GetInitialMaxData() { return _initial_max_data; }
    uint32_t GetInitialMaxStreamDataBidiLocal() { return _initial_max_stream_data_bidi_local; }
    uint32_t GetInitialMaxStreamDataBidiRemote() { return _initial_max_stream_data_bidi_remote; }
    uint32_t GetInitialMaxStreamDataUni() { return _initial_max_stream_data_uni; }
    uint32_t GetInitialMaxStreamsBidi() { return _initial_max_streams_bidi; }
    uint32_t GetInitialMaxStreamsUni() { return _initial_max_streams_uni; }
    uint32_t GetackDelayExponent() { return _ack_delay_exponent; }
    uint32_t GetMaxAckDelay() { return _max_ack_delay; }
    bool GetDisableActiveMigration() { return _disable_active_migration; }
    const std::string& GetPreferredAddress() { return _preferred_address; }
    uint32_t GetActiveConnectionIdLimit() { return _active_connection_id_limit; }
    const std::string& GetInitialSourceConnectionId() { return _initial_source_connection_id; }
    const std::string& GetRetrySourceConnectionId() { return _retry_source_connection_id; }

private:
    char* EncodeUint(char* start, char* end, uint32_t value, uint32_t type);
    char* EncodeString(char* start, char* end, const std::string& value, uint32_t type);
    char* EncodeBool(char* start, char* end, bool value, uint32_t type);

    char* DecodeUint(char* start, char* end, uint32_t& value);
    char* DecodeString(char* start, char* end, std::string& value);
    char* DecodeBool(char* start, char* end, bool& value);

private:
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