#ifndef QUIC_CONNECTION_TRANSPORT_PARAM
#define QUIC_CONNECTION_TRANSPORT_PARAM

#include <string>
#include <memory>
#include <cstdint>

namespace quicx {

class IBufferRead;
class IBufferWrite;
class TransportParamConfig;
class TransportParam {
public:
    TransportParam();
    ~TransportParam();

    // init transport param with local config
    void Init(TransportParamConfig& conf);

    // merge client and server transport param
    bool Merge(const TransportParam& tp);

    /*
     * serialization and deserialization operations
     */
    bool Encode(std::shared_ptr<IBufferWrite> buffer);
    bool Decode(std::shared_ptr<IBufferRead> buffer);
    uint32_t EncodeSize();

    /**
     * get transmission parameter interface cluster
     */
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
    /*
     * internal serialization and deserialization operations
     */
    uint8_t* EncodeUint(uint8_t* start, uint8_t* end, uint32_t value, uint32_t type);
    uint8_t* EncodeString(uint8_t* start, uint8_t* end, const std::string& value, uint32_t type);
    uint8_t* EncodeBool(uint8_t* start, uint8_t* end, bool value, uint32_t type);
    uint8_t* DecodeUint(uint8_t* start, uint8_t* end, uint32_t& value);
    uint8_t* DecodeString(uint8_t* start, uint8_t* end, std::string& value);
    uint8_t* DecodeBool(uint8_t* start, uint8_t* end, bool& value);

private:
    std::string _original_destination_connection_id;
    uint32_t    _max_idle_timeout;
    std::string _stateless_reset_token; // no client
    uint32_t    _max_udp_payload_size;
    uint32_t    _initial_max_data;
    uint32_t    _initial_max_stream_data_bidi_local;
    uint32_t    _initial_max_stream_data_bidi_remote;
    uint32_t    _initial_max_stream_data_uni;
    uint32_t    _initial_max_streams_bidi;
    uint32_t    _initial_max_streams_uni;
    uint32_t    _ack_delay_exponent; // no client
    uint32_t    _max_ack_delay;      // no client
    bool        _disable_active_migration; 
    std::string _preferred_address;  // no client
    uint32_t    _active_connection_id_limit;
    std::string _initial_source_connection_id; // no client
    std::string _retry_source_connection_id;   // no client
};

}

#endif