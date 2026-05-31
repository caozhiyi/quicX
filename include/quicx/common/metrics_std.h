#ifndef COMMON_METRICS_METRICS_STD
#define COMMON_METRICS_METRICS_STD

#include <quicx/common/metrics.h>

namespace quicx {
namespace common {

// Predefined Standard Metrics for quicX
// All IDs are initialized during Metrics::Initialize()
struct MetricsStd {
    // ==================== UDP Layer ====================
    static MetricID UdpPacketsRx;       // Total UDP packets received
    static MetricID UdpPacketsTx;       // Total UDP packets transmitted
    static MetricID UdpBytesRx;         // Total UDP bytes received
    static MetricID UdpBytesTx;         // Total UDP bytes transmitted
    static MetricID UdpDroppedPackets;  // Total UDP packets dropped
    static MetricID UdpSendErrors;      // Total UDP send errors

    // ==================== QUIC Connection ====================
    static MetricID QuicConnectionsActive;    // Current active connections (Gauge)
    static MetricID QuicConnectionsTotal;     // Total connections created
    static MetricID QuicConnectionsClosed;    // Total connections closed
    static MetricID QuicHandshakeSuccess;     // Successful handshakes
    static MetricID QuicHandshakeFail;        // Failed handshakes
    static MetricID QuicHandshakeDurationUs;  // Handshake duration (Histogram)

    // ==================== QUIC Packets ====================
    static MetricID QuicPacketsRx;          // Total QUIC packets received
    static MetricID QuicPacketsTx;          // Total QUIC packets transmitted
    static MetricID QuicPacketsRetransmit;  // Total retransmitted packets
    static MetricID QuicPacketsLost;        // Total lost packets
    static MetricID QuicPacketsDropped;     // Total dropped packets
    static MetricID QuicPacketsAcked;       // Total acknowledged packets

    // ==================== QUIC Streams ====================
    static MetricID QuicStreamsActive;   // Current active streams (Gauge)
    static MetricID QuicStreamsCreated;  // Total streams created
    static MetricID QuicStreamsClosed;   // Total streams closed
    static MetricID QuicStreamsBytesRx;  // Total bytes received on streams
    static MetricID QuicStreamsBytesTx;  // Total bytes transmitted on streams
    static MetricID QuicStreamsResetRx;  // Total received RESET_STREAM frames
    static MetricID QuicStreamsResetTx;  // Total sent RESET_STREAM frames

    // ==================== QUIC Flow Control ====================
    static MetricID QuicFlowControlBlocked;  // Times flow control blocked
    static MetricID QuicStreamDataBlocked;   // Times stream data blocked

    // ==================== HTTP/3 ====================
    static MetricID Http3RequestsTotal;      // Total HTTP/3 requests
    static MetricID Http3RequestsActive;     // Current active requests (Gauge)
    static MetricID Http3RequestsFailed;     // Failed HTTP/3 requests
    static MetricID Http3RequestDurationUs;  // Request duration (Histogram)
    static MetricID Http3ResponseBytesRx;    // Total response bytes received
    static MetricID Http3ResponseBytesTx;    // Total response bytes transmitted
    static MetricID Http3PushPromisesRx;     // Total push promises received
    static MetricID Http3PushPromisesTx;     // Total push promises sent

    // ==================== Congestion Control ====================
    static MetricID CongestionWindowBytes;  // Current congestion window (Gauge)
    static MetricID CongestionEventsTotal;  // Total congestion events
    static MetricID SlowStartExits;         // Times exited slow start
    static MetricID BytesInFlight;          // Current bytes in flight (Gauge)

    // ==================== Performance / Latency ====================
    static MetricID RttSmoothedUs;        // Smoothed RTT (Gauge)
    static MetricID RttVarianceUs;        // RTT variance (Gauge)
    static MetricID RttMinUs;             // Minimum RTT (Gauge)
    static MetricID PacketProcessTimeUs;  // Packet processing time (Histogram)

    // ==================== Memory / Resources ====================
    static MetricID MemPoolAllocatedBlocks;  // Current allocated blocks (Gauge)
    static MetricID MemPoolFreeBlocks;       // Current free blocks (Gauge)
    static MetricID MemPoolAllocations;      // Total allocations
    static MetricID MemPoolDeallocations;    // Total deallocations

    // ==================== Errors ====================
    static MetricID ErrorsProtocol;     // Protocol errors
    static MetricID ErrorsInternal;     // Internal errors
    static MetricID ErrorsFlowControl;  // Flow control errors
    static MetricID ErrorsStreamLimit;  // Stream limit errors

    // ==================== QUIC 0-RTT (Early Data) ====================
    static MetricID Quic0RttAccepted;  // 0-RTT data accepted
    static MetricID Quic0RttRejected;  // 0-RTT data rejected
    static MetricID Quic0RttBytesRx;   // 0-RTT bytes received
    static MetricID Quic0RttBytesTx;   // 0-RTT bytes transmitted

    // ==================== Path MTU Discovery ====================
    static MetricID PathMtuCurrent;  // Current path MTU (Gauge)
    static MetricID PathMtuUpdates;  // MTU update events

    // ==================== Connection Migration ====================
    static MetricID ConnectionMigrationsTotal;   // Total connection migrations
    static MetricID ConnectionMigrationsFailed;  // Failed connection migrations

    // ==================== Crypto / TLS ====================
    static MetricID TlsHandshakeDurationUs;  // TLS handshake duration (Histogram)
    static MetricID TlsSessionsResumed;      // TLS sessions resumed (0-RTT)
    static MetricID TlsSessionsCached;       // TLS sessions cached

    // ==================== Frame Statistics ====================
    static MetricID FramesRxTotal;  // Total frames received
    static MetricID FramesTxTotal;  // Total frames transmitted

    // = =================== HTTP/3 Status Codes ====================
    static MetricID Http3Responses2xx;  // 2xx responses
    static MetricID Http3Responses3xx;  // 3xx responses
    static MetricID Http3Responses4xx;  // 4xx responses
    static MetricID Http3Responses5xx;  // 5xx responses

    // ==================== Pacing (Send Rate Control) ====================
    static MetricID PacingRateBytesPerSec;  // Current pacing rate (Gauge)
    static MetricID PacingDelayUs;          // Pacing delay (Histogram)

    // ==================== ACK Related ====================
    static MetricID AckDelayUs;         // ACK delay (Histogram)
    static MetricID AckRangesPerFrame;  // ACK ranges per frame (Histogram)
    static MetricID AckFrequency;       // ACK frequency (Gauge)

    // ==================== Timeout Related ====================
    static MetricID IdleTimeoutTotal;       // Idle timeout events
    static MetricID PtoCountTotal;          // PTO (Probe Timeout) events
    static MetricID PtoCountPerConnection;  // PTO count distribution (Histogram)

    // ==================== Version Negotiation ====================
    static MetricID VersionNegotiationTotal;  // Version negotiation events
    static MetricID QuicVersionInUse;         // Current QUIC version (Gauge)

    // ==================== Retry Mechanism ====================
    static MetricID QuicRetryPacketsSent;       // Total Retry packets sent
    static MetricID QuicRetryByHighRate;        // Retry triggered by high connection rate
    static MetricID QuicRetryBySuspiciousIP;    // Retry triggered by suspicious IP
    static MetricID QuicRetryByPolicy;          // Retry triggered by ALWAYS policy
    static MetricID QuicRetryTokensValidated;   // Valid Retry tokens received
    static MetricID QuicRetryTokensInvalid;     // Invalid Retry tokens received

    // ==================== Diagnostic (formerly PerfProbe) ====================
    // Send-path latency decomposition (Histogram, μs)
    static MetricID DiagBuildLatencyUs;         // BuildDataPacket time
    static MetricID DiagSendLatencyUs;          // SendBuffer time
    static MetricID DiagSendtoLatencyUs;        // sendto() syscall time
    static MetricID DiagAckGapUs;              // Inter-ACK arrival gap

    // TrySend path outcomes (Counter)
    static MetricID DiagTrySendNoData;         // No data to send
    static MetricID DiagTrySendCwndBlocked;    // Cwnd blocked
    static MetricID DiagTrySendBuildFail;      // Build packet failed
    static MetricID DiagSendBufferFail;        // SendBuffer failed

    // Send loop wakeup (Counter)
    static MetricID DiagActiveSendCalls;       // ActiveSend() entries
    static MetricID DiagTrySendIters;          // TrySend() entries
    static MetricID DiagUdpOnRead;             // UdpReceiver::OnRead entries

    // SendManager outcomes (Counter)
    static MetricID DiagSendImmediateOk;       // CC allowed send
    static MetricID DiagSendBlockedCwnd;       // CC blocked (pacing/cwnd)
    static MetricID DiagSendAllDone;           // No data ready

    // Flow control (Counter)
    static MetricID DiagFlowControlBlocked;    // Connection/stream FC blocked

    // ACK aggregation (Counter)
    static MetricID DiagRecvAckThreshold;      // Queue hit threshold → immediate ACK
    static MetricID DiagRecvAckGap;            // Gap detected → immediate ACK
    static MetricID DiagRecvAckOoo;            // Out-of-order → immediate ACK
    static MetricID DiagRecvAckEcn;            // ECN CE → immediate ACK
    static MetricID DiagRecvAckInitial;        // Initial/Handshake → immediate ACK
    static MetricID DiagRecvAckDelayed;        // Delayed ACK

    // ACK receive path (Counter)
    static MetricID DiagAcksReceived;          // ACK frames received at SendControl

    // ACK generation (Counter)
    static MetricID DiagAckGenCalls;           // MayGenerateAckFrame entries
    static MetricID DiagAckGenEmitted;         // ACK frames actually emitted
    static MetricID DiagAckQueueDepth;         // Sum of queue depths at flush

    // UDP sender path (Counter)
    static MetricID DiagUdpSendCalls;          // UdpSender::Send entries
    static MetricID DiagUdpSendBatchCalls;     // UdpSender::SendBatch entries
    static MetricID DiagUdpSendOk;             // Send() succeeded
    static MetricID DiagUdpSendBatchOk;        // SendBatch() succeeded

    // Datagram fill diagnostics (Counter, sum-based for mean calculation)
    static MetricID DiagStreamSendSizeSum;     // Sum of STREAM frame data sizes
    static MetricID DiagStreamSendCount;       // Number of STREAM frames
    static MetricID DiagStreamSlackSum;        // Sum of stream FC slack
    static MetricID DiagVisitorLeftSum;        // Sum of visitor left size
    static MetricID DiagFirstChunkSum;         // Sum of first-chunk readable
    static MetricID DiagSendBufTotalSum;       // Sum of send_buffer total bytes
    static MetricID DiagSendBufChunksSum;      // Sum of send_buffer chunk count
    static MetricID DiagSendBufProbeCount;     // Number of buffer probes

    // Datagram fill distribution (Histogram, bytes)
    static MetricID DiagFirstChunkHist;        // First chunk size distribution
    static MetricID DiagSendSizeHist;          // STREAM frame send_size distribution
    static MetricID DiagPktPayloadHist;        // Packet payload size distribution
    static MetricID DiagPktPerIterHist;        // Packets per worker iteration
    static MetricID DiagSpanWriteHist;         // Write(span) data_len distribution
};

// Initialize all standard metrics
// Called internally by Metrics::Initialize()
void InitializeStandardMetrics();

}  // namespace common
}  // namespace quicx

#endif  // COMMON_METRICS_METRICS_STD_H
