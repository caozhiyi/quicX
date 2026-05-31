#ifndef QUIC_CONNECTION_REMOTE_TRANSPORT_PARAM_SNAPSHOT
#define QUIC_CONNECTION_REMOTE_TRANSPORT_PARAM_SNAPSHOT

#include <cstdint>

namespace quicx {
namespace quic {

class TransportParam;

// Value type that captures a snapshot of the remote peer's transport parameters.
// Used for 0-RTT session caching (RFC 9000 Section 7.4.1) so that clients can
// remember the server's limits from a previous handshake and apply them to
// early-data sending.
struct RemoteTransportParamSnapshot {
    bool has_value = false;
    uint64_t initial_max_data = 0;
    uint64_t initial_max_streams_bidi = 0;
    uint64_t initial_max_streams_uni = 0;
    uint64_t initial_max_stream_data_bidi_local = 0;
    uint64_t initial_max_stream_data_bidi_remote = 0;
    uint64_t initial_max_stream_data_uni = 0;
    uint64_t active_connection_id_limit = 0;

    // Factory: populate from a TransportParam received during handshake.
    static RemoteTransportParamSnapshot From(const TransportParam& tp);

    // Reset to default (no remembered params).
    void Reset() { *this = RemoteTransportParamSnapshot{}; }
};

}  // namespace quic
}  // namespace quicx

#endif