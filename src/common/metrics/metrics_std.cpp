#include "common/metrics/metrics_std.h"

namespace quicx {
namespace common {

// Static member definitions
MetricID MetricsStd::UdpPacketsRx = kInvalidMetricID;
MetricID MetricsStd::UdpPacketsTx = kInvalidMetricID;
MetricID MetricsStd::UdpBytesRx = kInvalidMetricID;
MetricID MetricsStd::UdpBytesTx = kInvalidMetricID;
MetricID MetricsStd::UdpDroppedPackets = kInvalidMetricID;
MetricID MetricsStd::UdpSendErrors = kInvalidMetricID;

MetricID MetricsStd::QuicConnectionsActive = kInvalidMetricID;
MetricID MetricsStd::QuicConnectionsTotal = kInvalidMetricID;
MetricID MetricsStd::QuicConnectionsClosed = kInvalidMetricID;
MetricID MetricsStd::QuicHandshakeSuccess = kInvalidMetricID;
MetricID MetricsStd::QuicHandshakeFail = kInvalidMetricID;
MetricID MetricsStd::QuicHandshakeDurationUs = kInvalidMetricID;

MetricID MetricsStd::QuicPacketsRx = kInvalidMetricID;
MetricID MetricsStd::QuicPacketsTx = kInvalidMetricID;
MetricID MetricsStd::QuicPacketsRetransmit = kInvalidMetricID;
MetricID MetricsStd::QuicPacketsLost = kInvalidMetricID;
MetricID MetricsStd::QuicPacketsDropped = kInvalidMetricID;
MetricID MetricsStd::QuicPacketsAcked = kInvalidMetricID;

MetricID MetricsStd::QuicStreamsActive = kInvalidMetricID;
MetricID MetricsStd::QuicStreamsCreated = kInvalidMetricID;
MetricID MetricsStd::QuicStreamsClosed = kInvalidMetricID;
MetricID MetricsStd::QuicStreamsBytesRx = kInvalidMetricID;
MetricID MetricsStd::QuicStreamsBytesTx = kInvalidMetricID;
MetricID MetricsStd::QuicStreamsResetRx = kInvalidMetricID;
MetricID MetricsStd::QuicStreamsResetTx = kInvalidMetricID;

MetricID MetricsStd::QuicFlowControlBlocked = kInvalidMetricID;
MetricID MetricsStd::QuicStreamDataBlocked = kInvalidMetricID;

MetricID MetricsStd::Http3RequestsTotal = kInvalidMetricID;
MetricID MetricsStd::Http3RequestsActive = kInvalidMetricID;
MetricID MetricsStd::Http3RequestsFailed = kInvalidMetricID;
MetricID MetricsStd::Http3RequestDurationUs = kInvalidMetricID;
MetricID MetricsStd::Http3ResponseBytesRx = kInvalidMetricID;
MetricID MetricsStd::Http3ResponseBytesTx = kInvalidMetricID;
MetricID MetricsStd::Http3PushPromisesRx = kInvalidMetricID;
MetricID MetricsStd::Http3PushPromisesTx = kInvalidMetricID;

MetricID MetricsStd::CongestionWindowBytes = kInvalidMetricID;
MetricID MetricsStd::CongestionEventsTotal = kInvalidMetricID;
MetricID MetricsStd::SlowStartExits = kInvalidMetricID;
MetricID MetricsStd::BytesInFlight = kInvalidMetricID;

MetricID MetricsStd::RttSmoothedUs = kInvalidMetricID;
MetricID MetricsStd::RttVarianceUs = kInvalidMetricID;
MetricID MetricsStd::RttMinUs = kInvalidMetricID;
MetricID MetricsStd::PacketProcessTimeUs = kInvalidMetricID;

MetricID MetricsStd::MemPoolAllocatedBlocks = kInvalidMetricID;
MetricID MetricsStd::MemPoolFreeBlocks = kInvalidMetricID;
MetricID MetricsStd::MemPoolAllocations = kInvalidMetricID;
MetricID MetricsStd::MemPoolDeallocations = kInvalidMetricID;

MetricID MetricsStd::ErrorsProtocol = kInvalidMetricID;
MetricID MetricsStd::ErrorsInternal = kInvalidMetricID;
MetricID MetricsStd::ErrorsFlowControl = kInvalidMetricID;
MetricID MetricsStd::ErrorsStreamLimit = kInvalidMetricID;

MetricID MetricsStd::Quic0RttAccepted = kInvalidMetricID;
MetricID MetricsStd::Quic0RttRejected = kInvalidMetricID;
MetricID MetricsStd::Quic0RttBytesRx = kInvalidMetricID;
MetricID MetricsStd::Quic0RttBytesTx = kInvalidMetricID;

MetricID MetricsStd::PathMtuCurrent = kInvalidMetricID;
MetricID MetricsStd::PathMtuUpdates = kInvalidMetricID;

MetricID MetricsStd::ConnectionMigrationsTotal = kInvalidMetricID;
MetricID MetricsStd::ConnectionMigrationsFailed = kInvalidMetricID;

MetricID MetricsStd::TlsHandshakeDurationUs = kInvalidMetricID;
MetricID MetricsStd::TlsSessionsResumed = kInvalidMetricID;
MetricID MetricsStd::TlsSessionsCached = kInvalidMetricID;

MetricID MetricsStd::FramesRxTotal = kInvalidMetricID;
MetricID MetricsStd::FramesTxTotal = kInvalidMetricID;

MetricID MetricsStd::Http3Responses2xx = kInvalidMetricID;
MetricID MetricsStd::Http3Responses3xx = kInvalidMetricID;
MetricID MetricsStd::Http3Responses4xx = kInvalidMetricID;
MetricID MetricsStd::Http3Responses5xx = kInvalidMetricID;

MetricID MetricsStd::PacingRateBytesPerSec = kInvalidMetricID;
MetricID MetricsStd::PacingDelayUs = kInvalidMetricID;

MetricID MetricsStd::AckDelayUs = kInvalidMetricID;
MetricID MetricsStd::AckRangesPerFrame = kInvalidMetricID;
MetricID MetricsStd::AckFrequency = kInvalidMetricID;

MetricID MetricsStd::IdleTimeoutTotal = kInvalidMetricID;
MetricID MetricsStd::PtoCountTotal = kInvalidMetricID;
MetricID MetricsStd::PtoCountPerConnection = kInvalidMetricID;

MetricID MetricsStd::VersionNegotiationTotal = kInvalidMetricID;
MetricID MetricsStd::QuicVersionInUse = kInvalidMetricID;

void InitializeStandardMetrics() {
    // UDP Layer
    MetricsStd::UdpPacketsRx = Metrics::RegisterCounter("udp_packets_rx", "Total UDP packets received");
    MetricsStd::UdpPacketsTx = Metrics::RegisterCounter("udp_packets_tx", "Total UDP packets transmitted");
    MetricsStd::UdpBytesRx = Metrics::RegisterCounter("udp_bytes_rx", "Total UDP bytes received");
    MetricsStd::UdpBytesTx = Metrics::RegisterCounter("udp_bytes_tx", "Total UDP bytes transmitted");
    MetricsStd::UdpDroppedPackets = Metrics::RegisterCounter("udp_dropped_packets", "Total UDP packets dropped");
    MetricsStd::UdpSendErrors = Metrics::RegisterCounter("udp_send_errors", "Total UDP send errors");

    // QUIC Connection
    MetricsStd::QuicConnectionsActive =
        Metrics::RegisterGauge("quic_connections_active", "Current active QUIC connections");
    MetricsStd::QuicConnectionsTotal =
        Metrics::RegisterCounter("quic_connections_total", "Total QUIC connections created");
    MetricsStd::QuicConnectionsClosed =
        Metrics::RegisterCounter("quic_connections_closed", "Total QUIC connections closed");
    MetricsStd::QuicHandshakeSuccess = Metrics::RegisterCounter("quic_handshake_success", "Successful QUIC handshakes");
    MetricsStd::QuicHandshakeFail = Metrics::RegisterCounter("quic_handshake_fail", "Failed QUIC handshakes");
    MetricsStd::QuicHandshakeDurationUs = Metrics::RegisterHistogram("quic_handshake_duration_us",
        "QUIC handshake duration in microseconds", {1000, 5000, 10000, 50000, 100000, 500000, 1000000}  // 1ms to 1s
    );

    // QUIC Packets
    MetricsStd::QuicPacketsRx = Metrics::RegisterCounter("quic_packets_rx", "Total QUIC packets received");
    MetricsStd::QuicPacketsTx = Metrics::RegisterCounter("quic_packets_tx", "Total QUIC packets transmitted");
    MetricsStd::QuicPacketsRetransmit =
        Metrics::RegisterCounter("quic_packets_retransmit", "Total retransmitted packets");
    MetricsStd::QuicPacketsLost = Metrics::RegisterCounter("quic_packets_lost", "Total lost packets");
    MetricsStd::QuicPacketsDropped = Metrics::RegisterCounter("quic_packets_dropped", "Total dropped packets");
    MetricsStd::QuicPacketsAcked = Metrics::RegisterCounter("quic_packets_acked", "Total acknowledged packets");

    // QUIC Streams
    MetricsStd::QuicStreamsActive = Metrics::RegisterGauge("quic_streams_active", "Current active streams");
    MetricsStd::QuicStreamsCreated = Metrics::RegisterCounter("quic_streams_created", "Total streams created");
    MetricsStd::QuicStreamsClosed = Metrics::RegisterCounter("quic_streams_closed", "Total streams closed");
    MetricsStd::QuicStreamsBytesRx =
        Metrics::RegisterCounter("quic_streams_bytes_rx", "Total bytes received on streams");
    MetricsStd::QuicStreamsBytesTx =
        Metrics::RegisterCounter("quic_streams_bytes_tx", "Total bytes transmitted on streams");
    MetricsStd::QuicStreamsResetRx =
        Metrics::RegisterCounter("quic_streams_reset_rx", "Total received RESET_STREAM frames");
    MetricsStd::QuicStreamsResetTx =
        Metrics::RegisterCounter("quic_streams_reset_tx", "Total sent RESET_STREAM frames");

    // QUIC Flow Control
    MetricsStd::QuicFlowControlBlocked =
        Metrics::RegisterCounter("quic_flow_control_blocked", "Times flow control blocked");
    MetricsStd::QuicStreamDataBlocked =
        Metrics::RegisterCounter("quic_stream_data_blocked", "Times stream data blocked");

    // HTTP/3
    MetricsStd::Http3RequestsTotal = Metrics::RegisterCounter("http3_requests_total", "Total HTTP/3 requests");
    MetricsStd::Http3RequestsActive = Metrics::RegisterGauge("http3_requests_active", "Current active HTTP/3 requests");
    MetricsStd::Http3RequestsFailed = Metrics::RegisterCounter("http3_requests_failed", "Failed HTTP/3 requests");
    MetricsStd::Http3RequestDurationUs = Metrics::RegisterHistogram("http3_request_duration_us",
        "HTTP/3 request duration in microseconds", {1000, 10000, 50000, 100000, 500000, 1000000, 5000000}  // 1ms to 5s
    );
    MetricsStd::Http3ResponseBytesRx =
        Metrics::RegisterCounter("http3_response_bytes_rx", "Total HTTP/3 response bytes received");
    MetricsStd::Http3ResponseBytesTx =
        Metrics::RegisterCounter("http3_response_bytes_tx", "Total HTTP/3 response bytes transmitted");
    MetricsStd::Http3PushPromisesRx =
        Metrics::RegisterCounter("http3_push_promises_rx", "Total push promises received");
    MetricsStd::Http3PushPromisesTx = Metrics::RegisterCounter("http3_push_promises_tx", "Total push promises sent");

    // Congestion Control
    MetricsStd::CongestionWindowBytes = Metrics::RegisterGauge("congestion_window_bytes", "Current congestion window");
    MetricsStd::CongestionEventsTotal = Metrics::RegisterCounter("congestion_events_total", "Total congestion events");
    MetricsStd::SlowStartExits = Metrics::RegisterCounter("slow_start_exits", "Times exited slow start");
    MetricsStd::BytesInFlight = Metrics::RegisterGauge("bytes_in_flight", "Current bytes in flight");

    // Performance / Latency
    MetricsStd::RttSmoothedUs = Metrics::RegisterGauge("rtt_smoothed_us", "Smoothed RTT in microseconds");
    MetricsStd::RttVarianceUs = Metrics::RegisterGauge("rtt_variance_us", "RTT variance in microseconds");
    MetricsStd::RttMinUs = Metrics::RegisterGauge("rtt_min_us", "Minimum RTT in microseconds");
    MetricsStd::PacketProcessTimeUs = Metrics::RegisterHistogram("packet_process_time_us",
        "Packet processing time in microseconds", {10, 50, 100, 500, 1000, 5000, 10000}  // 10us to 10ms
    );

    // Memory / Resources
    MetricsStd::MemPoolAllocatedBlocks =
        Metrics::RegisterGauge("mem_pool_allocated_blocks", "Current allocated memory blocks");
    MetricsStd::MemPoolFreeBlocks = Metrics::RegisterGauge("mem_pool_free_blocks", "Current free memory blocks");
    MetricsStd::MemPoolAllocations = Metrics::RegisterCounter("mem_pool_allocations", "Total memory allocations");
    MetricsStd::MemPoolDeallocations = Metrics::RegisterCounter("mem_pool_deallocations", "Total memory deallocations");

    MetricsStd::ErrorsProtocol = Metrics::RegisterCounter("errors_protocol", "Protocol errors");
    MetricsStd::ErrorsInternal = Metrics::RegisterCounter("errors_internal", "Internal errors");
    MetricsStd::ErrorsFlowControl = Metrics::RegisterCounter("errors_flow_control", "Flow control errors");
    MetricsStd::ErrorsStreamLimit = Metrics::RegisterCounter("errors_stream_limit", "Stream limit errors");

    // 0-RTT (Early Data)
    MetricsStd::Quic0RttAccepted = Metrics::RegisterCounter("quic_0rtt_accepted", "0-RTT data accepted");
    MetricsStd::Quic0RttRejected = Metrics::RegisterCounter("quic_0rtt_rejected", "0-RTT data rejected");
    MetricsStd::Quic0RttBytesRx = Metrics::RegisterCounter("quic_0rtt_bytes_rx", "0-RTT bytes received");
    MetricsStd::Quic0RttBytesTx = Metrics::RegisterCounter("quic_0rtt_bytes_tx", "0-RTT bytes transmitted");

    // Path MTU Discovery
    MetricsStd::PathMtuCurrent = Metrics::RegisterGauge("path_mtu_current", "Current path MTU");
    MetricsStd::PathMtuUpdates = Metrics::RegisterCounter("path_mtu_updates", "Path MTU update events");

    // Connection Migration
    MetricsStd::ConnectionMigrationsTotal =
        Metrics::RegisterCounter("connection_migrations_total", "Total connection migrations");
    MetricsStd::ConnectionMigrationsFailed =
        Metrics::RegisterCounter("connection_migrations_failed", "Failed connection migrations");

    // Crypto / TLS
    MetricsStd::TlsHandshakeDurationUs = Metrics::RegisterHistogram("tls_handshake_duration_us",
        "TLS handshake duration in microseconds", {500, 1000, 5000, 10000, 50000, 100000}  // 0.5ms to 100ms
    );
    MetricsStd::TlsSessionsResumed = Metrics::RegisterCounter("tls_sessions_resumed", "TLS sessions resumed (0-RTT)");
    MetricsStd::TlsSessionsCached = Metrics::RegisterCounter("tls_sessions_cached", "TLS sessions cached");

    // Frame Statistics
    MetricsStd::FramesRxTotal = Metrics::RegisterCounter("frames_rx_total", "Total frames received");
    MetricsStd::FramesTxTotal = Metrics::RegisterCounter("frames_tx_total", "Total frames transmitted");

    // HTTP/3 Status Codes
    MetricsStd::Http3Responses2xx = Metrics::RegisterCounter("http3_responses_2xx", "HTTP/3 2xx responses");
    MetricsStd::Http3Responses3xx = Metrics::RegisterCounter("http3_responses_3xx", "HTTP/3 3xx responses");
    MetricsStd::Http3Responses4xx = Metrics::RegisterCounter("http3_responses_4xx", "HTTP/3 4xx responses");
    MetricsStd::Http3Responses5xx = Metrics::RegisterCounter("http3_responses_5xx", "HTTP/3 5xx responses");

    // Pacing (Send Rate Control)
    MetricsStd::PacingRateBytesPerSec =
        Metrics::RegisterGauge("pacing_rate_bytes_per_sec", "Current pacing rate in bytes/sec");
    MetricsStd::PacingDelayUs = Metrics::RegisterHistogram(
        "pacing_delay_us", "Pacing delay in microseconds", {10, 50, 100, 500, 1000, 5000}  // 10us to 5ms
    );

    // ACK Related
    MetricsStd::AckDelayUs = Metrics::RegisterHistogram(
        "ack_delay_us", "ACK delay in microseconds", {100, 500, 1000, 5000, 10000, 50000}  // 0.1ms to 50ms
    );
    MetricsStd::AckRangesPerFrame =
        Metrics::RegisterHistogram("ack_ranges_per_frame", "Number of ACK ranges per frame", {1, 2, 5, 10, 20, 50});
    MetricsStd::AckFrequency = Metrics::RegisterGauge("ack_frequency", "ACK frequency (ACKs per second)");

    // Timeout Related
    MetricsStd::IdleTimeoutTotal = Metrics::RegisterCounter("idle_timeout_total", "Idle timeout events");
    MetricsStd::PtoCountTotal = Metrics::RegisterCounter("pto_count_total", "PTO (Probe Timeout) events");
    MetricsStd::PtoCountPerConnection = Metrics::RegisterHistogram(
        "pto_count_per_connection", "PTO count distribution per connection", {0, 1, 2, 5, 10, 20});

    // Version Negotiation
    MetricsStd::VersionNegotiationTotal =
        Metrics::RegisterCounter("version_negotiation_total", "Version negotiation events");
    MetricsStd::QuicVersionInUse = Metrics::RegisterGauge("quic_version_in_use", "Current QUIC version in use");
}

}  // namespace common
}  // namespace quicx
