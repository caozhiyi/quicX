// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#ifndef COMMON_QLOG_UTIL_QLOG_CONSTANTS
#define COMMON_QLOG_UTIL_QLOG_CONSTANTS

namespace quicx {
namespace common {

// qlog events
namespace QlogEvents {
// Connectivity events
constexpr const char* kServerListening = "quic:server_listening";
constexpr const char* kConnectionStarted = "quic:connection_started";
constexpr const char* kConnectionClosed = "quic:connection_closed";
constexpr const char* kConnectionIdUpdated = "quic:connection_id_updated";
constexpr const char* kConnectionStateUpdated = "quic:connection_state_updated";

// Transport events
constexpr const char* kPacketSent = "quic:packet_sent";
constexpr const char* kPacketReceived = "quic:packet_received";
constexpr const char* kPacketDropped = "quic:packet_dropped";
constexpr const char* kPacketBuffered = "quic:packet_buffered";
constexpr const char* kPacketsAcked = "quic:packets_acked";
constexpr const char* kStreamStateUpdated = "quic:stream_state_updated";

// Recovery events
constexpr const char* kRecoveryMetricsUpdated = "recovery:metrics_updated";
constexpr const char* kCongestionStateUpdated = "recovery:congestion_state_updated";
constexpr const char* kPacketLost = "recovery:packet_lost";
constexpr const char* kMarkedForRetransmit = "recovery:marked_for_retransmit";

// Security events
constexpr const char* kKeyUpdated = "security:key_updated";
constexpr const char* kKeyDiscarded = "security:key_discarded";

// HTTP/3 events
constexpr const char* kHttp3FrameCreated = "http3:frame_created";
constexpr const char* kHttp3FrameParsed = "http3:frame_parsed";
}  // namespace QlogEvents

// qlog version
constexpr const char* kQlogVersion = "0.4";
constexpr const char* kQlogFormat = "JSON-SEQ";

}  // namespace common
}  // namespace quicx

#endif
