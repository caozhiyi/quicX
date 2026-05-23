// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#ifndef COMMON_QLOG_UTIL_QLOG_CONSTANTS
#define COMMON_QLOG_UTIL_QLOG_CONSTANTS

namespace quicx {
namespace common {

// qlog events
//
// Event names follow draft-ietf-quic-qlog-quic-events (schema 11):
//   <category>:<name>
// where <category> is one of: connectivity, transport, security, recovery,
// http3, qpack. Custom (non-IETF) events use the "quicx:" namespace.
namespace QlogEvents {
// Connectivity events (RFC qlog quic-events §3)
constexpr const char* kServerListening = "connectivity:server_listening";
constexpr const char* kConnectionStarted = "connectivity:connection_started";
constexpr const char* kConnectionClosed = "connectivity:connection_closed";
constexpr const char* kConnectionIdUpdated = "connectivity:connection_id_updated";
constexpr const char* kConnectionStateUpdated = "connectivity:connection_state_updated";

// Transport events (RFC qlog quic-events §4)
constexpr const char* kPacketSent = "transport:packet_sent";
constexpr const char* kPacketReceived = "transport:packet_received";
constexpr const char* kPacketDropped = "transport:packet_dropped";
constexpr const char* kPacketBuffered = "transport:packet_buffered";
constexpr const char* kPacketsAcked = "transport:packets_acked";
constexpr const char* kStreamStateUpdated = "transport:stream_state_updated";

// Recovery events (RFC qlog quic-events §6)
constexpr const char* kRecoveryMetricsUpdated = "recovery:metrics_updated";
constexpr const char* kCongestionStateUpdated = "recovery:congestion_state_updated";
constexpr const char* kPacketLost = "recovery:packet_lost";
// Not a standard schema event; expose under custom namespace.
constexpr const char* kMarkedForRetransmit = "quicx:marked_for_retransmit";

// Security events (RFC qlog quic-events §5)
constexpr const char* kKeyUpdated = "security:key_updated";
constexpr const char* kKeyDiscarded = "security:key_discarded";

// HTTP/3 events
constexpr const char* kHttp3FrameCreated = "http3:frame_created";
constexpr const char* kHttp3FrameParsed = "http3:frame_parsed";
}  // namespace QlogEvents

// qlog version / format
//
// We emit JSON Text Sequences (RFC 7464):
//   * Each record is preceded by 0x1E (Record Separator) and terminated by \n.
//   * The first record is the "trace" object (qlog main schema draft-02 §6.2),
//     containing top-level fields (qlog_format / qlog_version / title /
//     description) plus a "trace" sub-object holding vantage_point /
//     common_fields / configuration. Subsequent records are events.
//
// This matches what qvis expects when loading a `.sqlog` file. Per
// draft-ietf-quic-qlog-main-schema-02, the canonical version string is
// "draft-02" and the canonical format identifier is "JSON-SEQ".
constexpr const char* kQlogVersion = "draft-02";
constexpr const char* kQlogFormat = "JSON-SEQ";
// IETF schema URI (informational, written into the trace's top-level record).
constexpr const char* kQlogSchemaQuic = "urn:ietf:params:qlog:schema:quic";

// Record Separator (0x1E) prefix per RFC 7464 / qlog JSON-SEQ.
constexpr char kJsonSeqRecordSeparator = '\x1E';

}  // namespace common
}  // namespace quicx

#endif
