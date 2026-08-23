#ifndef QUIC_CONNECTION_TRANSPORT_PARAM
#define QUIC_CONNECTION_TRANSPORT_PARAM

#include <cstdint>
#include <string>
#include <vector>

#include <quicx/quic/type.h>
#include "common/buffer/buffer_span.h"

namespace quicx {
namespace quic {

class TransportParamConfig;

// RFC 9000 §18.2 preferred_address binary format:
//   IPv4 (4) + IPv4 port (2) + IPv6 (16) + IPv6 port (2)
//   + CID length (1) + CID (0-20) + stateless reset token (16)
struct PreferredAddress {
    uint8_t ipv4[4] = {0, 0, 0, 0};
    uint16_t ipv4_port = 0;
    uint8_t ipv6[16] = {0};
    uint16_t ipv6_port = 0;
    std::string cid;                    // 0-20 bytes
    std::string stateless_reset_token;  // exactly 16 bytes
};

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
    // Decodes the peer's transport parameters and enforces the RFC 9000 §7.4 / §18.2
    // syntax and value-range rules. Returns false on any violation; the caller must
    // then close the connection with TRANSPORT_PARAMETER_ERROR.
    // @param received_by_server true when we are the server and these parameters came
    //        from the client, which makes the server-only parameters illegal (§18.2).
    bool Decode(const common::BufferSpan& buffer, bool received_by_server = false);
    uint32_t EncodeSize();

    // RFC 9000 §18.2 value bounds for peer-supplied parameters.
    static constexpr uint64_t kMaxAckDelayExponent = 20;
    static constexpr uint64_t kMaxAckDelayLimitMs = 1ULL << 14;  // max_ack_delay must be < 2^14
    static constexpr uint64_t kMinMaxUdpPayloadSize = 1200;
    static constexpr uint64_t kMinActiveConnectionIdLimit = 2;
    static constexpr uint64_t kMaxStreamsLimit = 1ULL << 60;
    static constexpr size_t kStatelessResetTokenLength = 16;
    static constexpr size_t kMaxTransportParamCidLength = 20;

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
    // True when the PEER declared disable_active_migration (set by Merge()).
    // A client consults this to decide whether it may actively migrate to a
    // new path (RFC 9000 §9.4); it is deliberately separate from
    // GetDisableActiveMigration(), which reflects OUR local declaration that
    // we encode on the wire.
    bool GetPeerDisableActiveMigration() const { return peer_disable_active_migration_; }
    // Raw wire bytes of the received preferred_address transport parameter
    // (client side; parse them with ParsePreferredAddressBinary from util.h).
    const std::string& GetPreferredAddress() const { return preferred_address_; }
    // Local configuration (not negotiated on the wire): client-side periodic
    // PING keep-alive, RFC 9000 §10.1.2. Interval derives from the negotiated
    // idle timeout.
    bool GetEnableKeepAlive() const { return enable_keep_alive_; }
    // Explicitly configured keep-alive cadence in ms; 0 = derive from the
    // negotiated idle timeout (max(idle/2, 1 s)).
    uint32_t GetKeepAliveInterval() const { return keep_alive_interval_ms_; }
    uint64_t GetActiveConnectionIdLimit() const { return active_connection_id_limit_; }

    // Client-only: the connection ID carried inside the RFC 9000 §18.2 binary
    // preferred_address structure. Per §9.6 the client MUST use this CID as the
    // DCID on the path to the preferred address. Empty unless a binary
    // preferred_address was received and successfully parsed.
    void SetPreferredAddressCID(const std::string& cid) { preferred_address_cid_ = cid; }
    const std::string& GetPreferredAddressCID() const { return preferred_address_cid_; }
    bool HasPreferredAddressCID() const { return !preferred_address_cid_.empty(); }

    // RFC 9000 §18.2 binary preferred_address (decoded from wire or prepared for encoding).
    const PreferredAddress& GetPreferredAddressBinary() const { return preferred_address_binary_; }
    bool HasPreferredAddressBinary() const { return has_preferred_address_binary_; }
    void SetPreferredAddressBinary(const PreferredAddress& addr) {
        preferred_address_binary_ = addr;
        has_preferred_address_binary_ = true;
    }

    const std::string& GetInitialSourceConnectionId() const { return initial_source_connection_id_; }
    const std::string& GetRetrySourceConnectionId() const { return retry_source_connection_id_; }

    // Server-only: RFC 9000 §7.3 requires the server to echo the Destination Connection ID
    // of the client's *first* Initial packet. Normally the accepting worker knows this value
    // before the connection object exists and passes it through QuicTransportParams; this
    // setter lets BaseConnection back-fill it from the first Initial when the caller did not,
    // so the parameter is never silently omitted.
    void SetOriginalDestinationConnectionId(const std::string& id) { original_destination_connection_id_ = id; }

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

    // Parse a human-readable "host:port" or "[host]:port" string into the
    // RFC 9000 §18.2 binary PreferredAddress fields (IPv4/IPv6 + port).
    bool ParsePreferredAddressString(const std::string& addr_str, PreferredAddress& out);

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
    uint64_t peer_initial_max_stream_data_bidi_remote_;  // peer's bidi_remote: our send limit on locally-initiated bidi
                                                         // streams
    uint64_t peer_initial_max_stream_data_bidi_local_;   // peer's bidi_local: our send limit on remotely-initiated bidi
                                                         // streams
    uint64_t initial_max_streams_bidi_;
    uint64_t initial_max_streams_uni_;
    uint64_t ack_delay_exponent_;  // no client
    uint64_t max_ack_delay_;       // no client
    bool disable_active_migration_;
    std::string preferred_address_;  // client: raw wire bytes of the received preferred_address param
    std::string preferred_address_cid_;  // no client: CID inside binary preferred_address
    PreferredAddress preferred_address_binary_;
    bool has_preferred_address_binary_ = false;
    bool enable_keep_alive_ = false;  // local only, never encoded on the wire
    uint32_t keep_alive_interval_ms_ = 0;  // local only; 0 = derive from idle timeout
    // Peer's declared disable_active_migration (captured by Merge()). Kept
    // separate from disable_active_migration_ (our own wire declaration) so
    // receiving a peer value can never pollute what we encode.
    bool peer_disable_active_migration_ = false;
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