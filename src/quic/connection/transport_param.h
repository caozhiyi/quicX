#ifndef QUIC_CONNECTION_TRANSPORT_PARAM
#define QUIC_CONNECTION_TRANSPORT_PARAM

#include <string>
#include <memory>
#include <cstdint>
#include "quic/include/type.h"
#include "common/buffer/if_buffer_read.h"
#include "common/buffer/if_buffer_write.h"

namespace quicx {
namespace quic {


class TransportParamConfig;
class TransportParam {
public:
    TransportParam();
    ~TransportParam();

    void AddTransportParamListener(std::function<void(const TransportParam&)> listener);

    // init transport param with local config
    void Init(const QuicTransportParams& conf);

    // merge client and server transport param
    bool Merge(const TransportParam& tp);

    /*
     * serialization and deserialization operations
     */
    bool Encode(std::shared_ptr<common::IBufferWrite> buffer);
    bool Decode(std::shared_ptr<common::IBufferRead> buffer);
    uint32_t EncodeSize();

    /**
     * get transmission parameter interface cluster
     */
    const std::string& GetOriginalDestinationConnectionId() const { return original_destination_connection_id_; }
    uint32_t GetMaxIdleTimeout() const { return max_idle_timeout_; }
    const std::string& GetStatelessResetToken() const { return stateless_reset_token_; }
    uint32_t GetmaxUdpPayloadSize() const { return max_udp_payload_size_; }
    uint32_t GetInitialMaxData() const { return initial_max_data_; }
    uint32_t GetInitialMaxStreamDataBidiLocal() { return initial_max_stream_data_bidi_local_; }
    uint32_t GetInitialMaxStreamDataBidiRemote() const { return initial_max_stream_data_bidi_remote_; }
    uint32_t GetInitialMaxStreamDataUni() const { return initial_max_stream_data_uni_; }
    uint32_t GetInitialMaxStreamsBidi() const { return initial_max_streams_bidi_; }
    uint32_t GetInitialMaxStreamsUni() const { return initial_max_streams_uni_; }
    uint32_t GetackDelayExponent() const { return ack_delay_exponent_; }
    uint32_t GetMaxAckDelay() const { return max_ack_delay_; }
    bool GetDisableActiveMigration() const { return disable_active_migration_; }
    const std::string& GetPreferredAddress() const { return preferred_address_; }
    uint32_t GetActiveConnectionIdLimit() const { return active_connection_id_limit_; }
    
    // Server-only: Set preferred address for client migration suggestion
    // Format: "ip:port" (e.g., "192.168.1.100:8443")
    // The server advertises this address to suggest the client migrate to it
    void SetPreferredAddress(const std::string& addr) { preferred_address_ = addr; }
    const std::string& GetInitialSourceConnectionId() const { return initial_source_connection_id_; }
    const std::string& GetRetrySourceConnectionId() const { return retry_source_connection_id_; }

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
    std::string original_destination_connection_id_;
    uint32_t    max_idle_timeout_;
    std::string stateless_reset_token_; // no client
    uint32_t    max_udp_payload_size_;
    uint32_t    initial_max_data_;
    uint32_t    initial_max_stream_data_bidi_local_;
    uint32_t    initial_max_stream_data_bidi_remote_;
    uint32_t    initial_max_stream_data_uni_;
    uint32_t    initial_max_streams_bidi_;
    uint32_t    initial_max_streams_uni_;
    uint32_t    ack_delay_exponent_; // no client
    uint32_t    max_ack_delay_;      // no client
    bool        disable_active_migration_; 
    std::string preferred_address_;  // no client
    uint32_t    active_connection_id_limit_;
    std::string initial_source_connection_id_; // no client
    std::string retry_source_connection_id_;   // no client

private:
    std::vector<std::function<void(const TransportParam&)>> transport_param_listeners_;
};

}
}

#endif