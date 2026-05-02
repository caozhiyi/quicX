#ifndef QUIC_CONNECTION_TRANSPORT_PARAM
#define QUIC_CONNECTION_TRANSPORT_PARAM

#include <cstdint>
#include <string>
#include <vector>

#include "common/buffer/buffer_span.h"
#include "quic/include/type.h"

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
    bool Encode(const common::BufferSpan& buffer, size_t& bytes_written);
    bool Decode(const common::BufferSpan& buffer);
    uint32_t EncodeSize();

    /**
     * get transmission parameter interface cluster
     */
    const std::string& GetOriginalDestinationConnectionId() const { return original_destination_connection_id_; }
    uint64_t GetMaxIdleTimeout() const { return max_idle_timeout_; }
    const std::string& GetStatelessResetToken() const { return stateless_reset_token_; }
    uint64_t GetmaxUdpPayloadSize() const { return max_udp_payload_size_; }
    uint64_t GetInitialMaxData() const { return initial_max_data_; }
    uint64_t GetInitialMaxStreamDataBidiLocal() const { return initial_max_stream_data_bidi_local_; }
    uint64_t GetInitialMaxStreamDataBidiRemote() const { return initial_max_stream_data_bidi_remote_; }
    uint64_t GetInitialMaxStreamDataUni() const { return initial_max_stream_data_uni_; }
    // Peer transport param values (set during Merge)
    // For locally-initiated bidi streams: send limit = peer's bidi_remote
    uint64_t GetPeerInitialMaxStreamDataBidiRemote() const { return peer_initial_max_stream_data_bidi_remote_; }
    // For remotely-initiated bidi streams: send limit = peer's bidi_local
    uint64_t GetPeerInitialMaxStreamDataBidiLocal() const { return peer_initial_max_stream_data_bidi_local_; }
    uint64_t GetInitialMaxStreamsBidi() const { return initial_max_streams_bidi_; }
    uint64_t GetInitialMaxStreamsUni() const { return initial_max_streams_uni_; }
    uint64_t GetackDelayExponent() const { return ack_delay_exponent_; }
    uint64_t GetMaxAckDelay() const { return max_ack_delay_; }
    bool GetDisableActiveMigration() const { return disable_active_migration_; }
    const std::string& GetPreferredAddress() const { return preferred_address_; }
    uint64_t GetActiveConnectionIdLimit() const { return active_connection_id_limit_; }

    // Server-only: Set preferred address for client migration suggestion
    // Format: "ip:port" (e.g., "192.168.1.100:8443")
    // The server advertises this address to suggest the client migrate to it
    void SetPreferredAddress(const std::string& addr) { preferred_address_ = addr; }
    const std::string& GetInitialSourceConnectionId() const { return initial_source_connection_id_; }
    const std::string& GetRetrySourceConnectionId() const { return retry_source_connection_id_; }

    // RFC 9368 Compatible Version Negotiation: version_information transport parameter (id 0x11)
    // chosen_version:     the version that the sender is using for this connection
    // available_versions: versions the sender supports, in preference order (first == most preferred)
    void SetVersionInformation(uint32_t chosen_version, const std::vector<uint32_t>& available_versions) {
        chosen_version_ = chosen_version;
        available_versions_ = available_versions;
        has_version_information_ = true;
    }
    bool HasVersionInformation() const { return has_version_information_; }
    uint32_t GetChosenVersion() const { return chosen_version_; }
    const std::vector<uint32_t>& GetAvailableVersions() const { return available_versions_; }

private:
    /*
     * internal serialization and deserialization operations
     */
    uint8_t* EncodeUint(uint8_t* start, uint8_t* end, uint64_t value, uint32_t type);
    uint8_t* EncodeString(uint8_t* start, uint8_t* end, const std::string& value, uint32_t type);
    uint8_t* EncodeBool(uint8_t* start, uint8_t* end, bool value, uint32_t type);
    uint8_t* DecodeUint(uint8_t* start, uint8_t* end, uint64_t& value);
    uint8_t* DecodeString(uint8_t* start, uint8_t* end, std::string& value);
    uint8_t* DecodeBool(uint8_t* start, uint8_t* end, bool& value);

private:
    std::string original_destination_connection_id_;
    uint64_t max_idle_timeout_;
    std::string stateless_reset_token_;  // no client
    uint64_t max_udp_payload_size_;
    uint64_t initial_max_data_;
    uint64_t initial_max_stream_data_bidi_local_;
    uint64_t initial_max_stream_data_bidi_remote_;
    uint64_t initial_max_stream_data_uni_;
    // Peer's transport param values (populated during Merge)
    uint64_t peer_initial_max_stream_data_bidi_remote_;  // peer's bidi_remote: our send limit on locally-initiated bidi streams
    uint64_t peer_initial_max_stream_data_bidi_local_;   // peer's bidi_local: our send limit on remotely-initiated bidi streams
    uint64_t initial_max_streams_bidi_;
    uint64_t initial_max_streams_uni_;
    uint64_t ack_delay_exponent_;  // no client
    uint64_t max_ack_delay_;       // no client
    bool disable_active_migration_;
    std::string preferred_address_;  // no client
    uint64_t active_connection_id_limit_;
    std::string initial_source_connection_id_;  // no client
    std::string retry_source_connection_id_;    // no client

    // RFC 9368 version_information (TP 0x11)
    bool has_version_information_ = false;
    uint32_t chosen_version_ = 0;
    std::vector<uint32_t> available_versions_;

private:
    std::vector<std::function<void(const TransportParam&)>> transport_param_listeners_;
};

}  // namespace quic
}  // namespace quicx

#endif