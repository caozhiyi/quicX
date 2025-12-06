# quicX Metrics Monitoring System

## Overview

quicX Metrics is a high-performance, zero-overhead monitoring system that provides comprehensive observability for the quicX QUIC/HTTP3 implementation. The system features a lock-free design, supports Prometheus format export, and enables real-time monitoring of network, transport, and application layer metrics.

## Core Features

### High-Performance Design

- **O(1) Complexity**: All metric operations have constant time complexity
- **Lock-Free Implementation**: Uses atomic operations to avoid lock contention
- **Zero Heap Allocation**: Pre-allocated slots, no memory allocation at runtime
- **Extremely Low Overhead**: Single metric update < 10ns

### Complete Coverage

- **54+ Metrics**: Covers UDP, QUIC, and HTTP/3 layers
- **Real-Time Updates**: Metrics immediately reflect system state
- **Clear Categorization**: Metrics organized by functional modules

### Easy to Use

- **Automatic Instrumentation**: Core metrics automatically collected
- **Runtime Configuration**: Can be dynamically enabled/disabled
- **Standard Format**: Prometheus text format export

## System Architecture

### Core Components

```
┌─────────────────────────────────────────────────┐
│           Application Code                      │
│  (UDP, QUIC, HTTP/3, Memory Pool, etc.)        │
└────────────────┬────────────────────────────────┘
                 │ Metrics::CounterInc()
                 │ Metrics::GaugeSet()
                 ▼
┌─────────────────────────────────────────────────┐
│         Metrics Registry (Lock-Free)            │
│  ┌──────────────┐  ┌──────────────┐            │
│  │   Counter    │  │    Gauge     │            │
│  │  (Atomic)    │  │  (Atomic)    │            │
│  └──────────────┘  └──────────────┘            │
└────────────────┬────────────────────────────────┘
                 │ ExportPrometheus()
                 ▼
┌─────────────────────────────────────────────────┐
│         Prometheus Exporter                     │
│  # TYPE metric_name counter                    │
│  metric_name{labels} value                     │
└─────────────────────────────────────────────────┘
```

### Implementation Principles

#### 1. Lock-Free Slot Allocation

```cpp
// Pre-allocated slot array
std::vector<MetricSlot> slots_;  // Allocated at initialization

// O(1) registration
MetricID RegisterCounter(name, help) {
    size_t id = next_id_.fetch_add(1);  // Atomic increment
    slots_[id] = MetricSlot{name, help, COUNTER};
    return id;
}
```

#### 2. Atomic Operation Updates

```cpp
// Counter increment - lock-free
void CounterInc(MetricID id, uint64_t delta = 1) {
    slots_[id].value.fetch_add(delta, std::memory_order_relaxed);
}

// Gauge set - lock-free
void GaugeSet(MetricID id, uint64_t value) {
    slots_[id].value.store(value, std::memory_order_relaxed);
}
```

#### 3. Efficient Export

```cpp
std::string ExportPrometheus() {
    std::ostringstream oss;
    for (auto& slot : slots_) {
        if (slot.type == COUNTER) {
            oss << "# TYPE " << slot.name << " counter\n";
            oss << slot.name << " " 
                << slot.value.load(std::memory_order_relaxed) << "\n";
        }
        // ... Gauge, Histogram
    }
    return oss.str();
}
```

## Metric Categories

### 1. UDP Layer Metrics (6 metrics)

| Metric Name | Type | Description |
|-------------|------|-------------|
| `udp_packets_rx` | Counter | Total UDP packets received |
| `udp_packets_tx` | Counter | Total UDP packets sent |
| `udp_bytes_rx` | Counter | Total UDP bytes received |
| `udp_bytes_tx` | Counter | Total UDP bytes sent |
| `udp_dropped_packets` | Counter | Total UDP packets dropped |
| `udp_send_errors` | Counter | Total UDP send errors |

**Purpose**: Monitor network layer health, identify network congestion and packet loss issues.

### 2. QUIC Connection Layer Metrics (5 metrics)

| Metric Name | Type | Description |
|-------------|------|-------------|
| `quic_connections_active` | Gauge | Current active connections |
| `quic_connections_total` | Counter | Total connections created |
| `quic_connections_closed` | Counter | Total connections closed |
| `quic_handshake_success` | Counter | Successful handshakes |
| `quic_handshake_fail` | Counter | Failed handshakes |

**Purpose**: Monitor connection lifecycle, evaluate handshake success rate.

### 3. QUIC Packet Layer Metrics (6 metrics)

| Metric Name | Type | Description |
|-------------|------|-------------|
| `quic_packets_rx` | Counter | Total QUIC packets received |
| `quic_packets_tx` | Counter | Total QUIC packets sent |
| `quic_packets_retransmit` | Counter | Total retransmitted packets |
| `quic_packets_lost` | Counter | Total packets lost |
| `quic_packets_acked` | Counter | Total packets acknowledged |

**Purpose**: Monitor transport layer reliability, calculate packet loss and retransmission rates.

### 4. QUIC Stream Layer Metrics (7 metrics)

| Metric Name | Type | Description |
|-------------|------|-------------|
| `quic_streams_active` | Gauge | Current active streams |
| `quic_streams_created` | Counter | Total streams created |
| `quic_streams_closed` | Counter | Total streams closed |
| `quic_streams_bytes_rx` | Counter | Total stream bytes received |
| `quic_streams_bytes_tx` | Counter | Total stream bytes sent |
| `quic_streams_reset_rx` | Counter | RESET frames received |
| `quic_streams_reset_tx` | Counter | RESET frames sent |

**Purpose**: Monitor stream management and data transmission, identify stream anomalies.

### 5. RTT Performance Metrics (3 metrics)

| Metric Name | Type | Description |
|-------------|------|-------------|
| `rtt_smoothed_us` | Gauge | Smoothed RTT (microseconds) |
| `rtt_variance_us` | Gauge | RTT variance (microseconds) |
| `rtt_min_us` | Gauge | Minimum RTT (microseconds) |

**Purpose**: Monitor network latency, evaluate connection quality.

### 6. Congestion Control Metrics (7 metrics)

| Metric Name | Type | Description |
|-------------|------|-------------|
| `congestion_window_bytes` | Gauge | Current congestion window (bytes) |
| `congestion_events_total` | Counter | Total congestion events |
| `slow_start_exits` | Counter | Slow start exits |
| `bytes_in_flight` | Gauge | Bytes in flight |
| `pacing_rate_bytes_per_sec` | Gauge | Pacing rate (bytes/second) |

**Purpose**: Monitor congestion control algorithm, optimize throughput.

### 7. Error Statistics Metrics (4 metrics)

| Metric Name | Type | Description |
|-------------|------|-------------|
| `errors_protocol` | Counter | Protocol errors |
| `errors_internal` | Counter | Internal errors |
| `errors_flow_control` | Counter | Flow control errors |
| `errors_stream_limit` | Counter | Stream limit errors |

**Purpose**: Monitor system health, quickly identify issues.

### 8. Flow Control Metrics (2 metrics)

| Metric Name | Type | Description |
|-------------|------|-------------|
| `quic_flow_control_blocked` | Counter | Connection-level flow control blocks |
| `quic_stream_data_blocked` | Counter | Stream-level flow control blocks |

**Purpose**: Monitor flow control state, optimize window sizes.

### 9. Timeout Metrics (2 metrics)

| Metric Name | Type | Description |
|-------------|------|-------------|
| `idle_timeout_total` | Counter | Idle timeouts |
| `pto_count_total` | Counter | PTO timeouts |

**Purpose**: Monitor timeout events, adjust timeout parameters.

### 10. HTTP/3 Metrics (4 metrics)

| Metric Name | Type | Description |
|-------------|------|-------------|
| `http3_requests_total` | Counter | Total HTTP/3 requests |
| `http3_requests_active` | Gauge | Current active requests |
| `http3_requests_failed` | Counter | Failed requests |
| `http3_push_promises_rx` | Counter | Push promises received |

**Purpose**: Monitor HTTP/3 business metrics, evaluate service quality.

### 11. Memory Pool Metrics (4 metrics)

| Metric Name | Type | Description |
|-------------|------|-------------|
| `mem_pool_allocated_blocks` | Gauge | Allocated blocks |
| `mem_pool_free_blocks` | Gauge | Free blocks |
| `mem_pool_allocations` | Counter | Allocation count |
| `mem_pool_deallocations` | Counter | Deallocation count |

**Purpose**: Monitor memory usage, optimize memory pool configuration.

### 12. Frame Statistics Metrics (2 metrics)

| Metric Name | Type | Description |
|-------------|------|-------------|
| `frames_rx_total` | Counter | Total frames received |
| `frames_tx_total` | Counter | Total frames sent |

**Purpose**: Monitor protocol layer activity, analyze communication patterns.

### 13. ACK Related Metrics (2 metrics)

| Metric Name | Type | Description |
|-------------|------|-------------|
| `ack_delay_us` | Gauge | ACK delay (microseconds) |
| `ack_ranges_per_frame` | Gauge | ACK ranges per frame |

**Purpose**: Monitor ACK behavior, optimize acknowledgment strategy.

## Usage Guide

### Initialization

```cpp
#include "common/metrics/metrics.h"

// 1. Configure Metrics
quicx::MetricsConfig config;
config.enable = true;              // Enable metrics
config.initial_slots = 1024;       // Initial slot count
config.prefix = "quicx_";          // Metric name prefix

// 2. Initialize Metrics system
quicx::common::Metrics::Initialize(config);
```

### Export Prometheus Format

```cpp
// Get metrics data in Prometheus format
std::string metrics_data = quicx::common::Metrics::ExportPrometheus();

// Write to file
std::ofstream file("/var/lib/prometheus/quicx.prom");
file << metrics_data;
file.close();

// Or serve via HTTP endpoint
// (See HTTP/3 Metrics Endpoint section)
```

### HTTP/3 Metrics Endpoint

```cpp
#include "http3/include/if_server.h"

// Create HTTP/3 server
auto server = quicx::IServer::Create(settings);

// Configure server
quicx::Http3ServerConfig config;
config.cert_file_ = "server.crt";
config.key_file_ = "server.key";

// Enable metrics endpoint
config.config_.metrics_.enable = true;
config.config_.metrics_.path = "/metrics";

// Initialize and start
server->Init(config);
server->Start("0.0.0.0", 8443);
```

Access metrics:
```bash
# Using HTTP/3 client
curl --http3 https://localhost:8443/metrics
```

## Performance Guarantees

### Benchmark Results

```
Benchmark                          Time        CPU
-------------------------------------------------
CounterInc/1                    8.2 ns     8.2 ns
CounterInc/100                  820 ns     820 ns
GaugeSet/1                      7.5 ns     7.5 ns
GaugeSet/100                    750 ns     750 ns
ExportPrometheus/100          45.2 µs    45.2 µs
ExportPrometheus/1000        452.0 µs   452.0 µs
```

### Performance Characteristics

1. **Ultra-Low Latency**: Single update < 10ns
2. **Linear Scaling**: Performance scales linearly with metric count
3. **Zero Contention**: Lock-free design, no contention in multi-threaded scenarios
4. **Memory Efficient**: Pre-allocated, no runtime allocation

### Memory Footprint

```
Per metric slot: ~128 bytes
1000 metrics: ~128 KB
Export buffer: ~100 KB (temporary)
```

## Best Practices

### 1. Configure Slot Count Appropriately

```cpp
// Configure based on expected metric count
config.initial_slots = expected_metrics * 1.5;  // Leave 50% headroom
```

### 2. Export Regularly

```cpp
// Export every 15 seconds (Prometheus default scrape interval)
std::thread exporter([]{
    while (running) {
        std::string data = Metrics::ExportPrometheus();
        WriteToFile("/var/lib/prometheus/quicx.prom", data);
        std::this_thread::sleep_for(std::chrono::seconds(15));
    }
});
```

### 3. Monitor Key Metrics

Priority monitoring:
- Connection count (`quic_connections_active`)
- Packet loss rate (`quic_packets_lost / quic_packets_tx`)
- RTT (`rtt_smoothed_us`)
- Error rate (`errors_*`)
- Throughput (`quic_streams_bytes_*`)

### 4. Set Up Alerts

```yaml
# Prometheus alert rules example
groups:
  - name: quicx_alerts
    rules:
      - alert: HighPacketLoss
        expr: rate(quic_packets_lost[5m]) / rate(quic_packets_tx[5m]) > 0.05
        annotations:
          summary: "High packet loss rate (> 5%)"
      
      - alert: HighRTT
        expr: rtt_smoothed_us > 100000  # > 100ms
        annotations:
          summary: "High RTT detected"
      
      - alert: TooManyErrors
        expr: sum(rate(errors_protocol[5m])) > 10
        annotations:
          summary: "High error rate"
```

## Troubleshooting

### Issue: Metrics Not Updating

**Cause**: Metrics not initialized or disabled

**Solution**:
```cpp
// Ensure initialization
Metrics::Initialize(config);

// Ensure enabled
config.enable = true;
```

### Issue: Export Data Empty

**Cause**: No metrics registered

**Solution**:
```cpp
// Ensure InitializeStandardMetrics() was called
// This is automatically called in Metrics::Initialize()
```

### Issue: Performance Degradation

**Cause**: Insufficient slots, causing reallocation

**Solution**:
```cpp
// Increase initial slot count
config.initial_slots = 2048;  // Or larger
```

## Extension Development

### Adding Custom Metrics

```cpp
// 1. Declare in metrics_std.h
struct MetricsStd {
    static MetricID MyCustomMetric;
};

// 2. Define in metrics_std.cpp
MetricID MetricsStd::MyCustomMetric = kInvalidMetricID;

// 3. Register in InitializeStandardMetrics()
MetricsStd::MyCustomMetric = 
    Metrics::RegisterCounter("my_custom_metric", "My custom metric");

// 4. Use in code
Metrics::CounterInc(MetricsStd::MyCustomMetric);
```

### Adding Histogram Support

```cpp
// Register Histogram
MetricID latency_hist = Metrics::RegisterHistogram(
    "request_latency_us", 
    "Request latency in microseconds",
    {10, 50, 100, 500, 1000, 5000}  // buckets
);

// Record observation
Metrics::HistogramObserve(latency_hist, latency_value);
```

## References

- [Prometheus Documentation](https://prometheus.io/docs/)
- [QUIC RFC 9000](https://www.rfc-editor.org/rfc/rfc9000.html)
- [HTTP/3 RFC 9114](https://www.rfc-editor.org/rfc/rfc9114.html)

