# Metrics Monitoring Example

This example demonstrates how to use the quicX metrics system for real-time monitoring and performance analysis.

## Features

- [x] **Custom Metrics Registration** - Register application-level Counters, Gauges, and Histograms
- [x] **Automatic Collection** - Auto-collection of protocol-level metrics (QUIC/HTTP/3)
- [x] **Prometheus Export** - Export metrics in standard Prometheus format
- [x] **Real-time Monitoring** - Periodic printing of key metrics to console
- [x] **HTTP Endpoints** - Access metrics via HTTP endpoints
- [x] **Visual Dashboard** - Simple HTML dashboard served directly by the app

## Quick Start

### 1. Build

```bash
cd build
cmake ..
make metrics_monitoring_server metrics_monitoring_client
```

### 2. Run Server

```bash
./bin/metrics_monitoring_server
```

The server will start on `https://0.0.0.0:8443` and expose the following endpoints:

| Endpoint | Description |
|----------|-------------|
| `/hello` | Simple hello endpoint |
| `/slow` | Slow endpoint (500ms delay) |
| `/error` | Error endpoint (returns 500 status) |
| `/metrics` | Prometheus format metrics export |
| `/dashboard` | Simple HTML visualization dashboard |

### 3. Run Test Client

In another terminal:

```bash
./bin/metrics_monitoring_client
```

The client will:
1. Perform basic functionality tests on all endpoints
2. Execute a load test (10 concurrent requests)
3. Fetch and display server metrics

### 4. View Metrics

You can simple use `curl` to fetch the metrics:

```bash
# Get Prometheus format metrics
curl -k https://localhost:8443/metrics

# Get HTML dashboard
curl -k https://localhost:8443/dashboard > dashboard.html
# Open dashboard.html in your browser
```

## Code Guide

### 1. Initialize Metrics System

```cpp
#include "common/metrics/metrics.h"

// Initialize configuration
quicx::MetricsConfig config;
config.enable = true;
config.prefix = "quicx";
quicx::common::Metrics::Initialize(config);
```

### 2. Register Custom Metrics

```cpp
// Counter: Monotonically increasing counter
quicx::common::MetricID requests_total = 
    quicx::common::Metrics::RegisterCounter(
        "custom_requests_total",
        "Total number of requests"
    );

// Gauge: Value that can go up and down
quicx::common::MetricID active_requests = 
    quicx::common::Metrics::RegisterGauge(
        "custom_active_requests",
        "Number of active requests"
    );

// Histogram: Statistical distribution
quicx::common::MetricID request_duration = 
    quicx::common::Metrics::RegisterHistogram(
        "custom_request_duration_ms",
        "Request duration in milliseconds",
        {10, 25, 50, 100, 250, 500, 1000}  // Buckets
    );
```

### 3. Update Metrics

```cpp
// Counter: Increment
quicx::common::Metrics::CounterInc(requests_total);

// Gauge: Increment/Decrement/Set
quicx::common::Metrics::GaugeInc(active_requests);
quicx::common::Metrics::GaugeDec(active_requests);
quicx::common::Metrics::GaugeSet(active_requests, 10);

// Histogram: Observe value
quicx::common::Metrics::HistogramObserve(request_duration, 123);
```

### 4. Export to Prometheus Format

```cpp
std::string metrics = quicx::common::Metrics::ExportPrometheus();
// Send 'metrics' string as HTTP response body
```

### 5. RAII Pattern for Tracking (Recommended)

```cpp
class RequestTracker {
public:
    RequestTracker() {
        quicx::common::Metrics::GaugeInc(active_requests);
        start_time_ = std::chrono::steady_clock::now();
    }
    
    ~RequestTracker() {
        quicx::common::Metrics::GaugeDec(active_requests);
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time_).count();
        quicx::common::Metrics::HistogramObserve(request_duration, duration);
    }
    
private:
    std::chrono::steady_clock::time_point start_time_;
};

// Usage
void HandleRequest() {
    RequestTracker tracker;  // Automatically tracks upon creation
    // ... process request ...
}  // Metrics updated automatically when tracker goes out of scope
```

## Metric Definitions

### Custom Application Metrics (in this example)

| Metric Name | Type | Description |
|-------------|------|-------------|
| `custom_requests_total` | Counter | Total requests processed |
| `custom_active_requests` | Gauge | Currently active requests |
| `custom_request_duration_ms` | Histogram | Request processing duration (ms) |
| `custom_error_count` | Counter | Total error count |

### Standard QUIC/HTTP/3 Metrics (Auto-collected)

#### UDP Layer
- `quicx_udp_packets_rx/tx` - UDP packets received/sent
- `quicx_udp_bytes_rx/tx` - UDP bytes received/sent
- `quicx_udp_send_errors` - UDP send errors

#### QUIC Connection
- `quicx_quic_connections_active` - Currently active connections
- `quicx_quic_handshake_success` - Successful handshakes
- `quicx_quic_handshake_duration_us` - Handshake duration

#### QUIC Packets
- `quicx_quic_packets_rx/tx` - QUIC packets received/sent
- `quicx_quic_packets_lost` - Packets lost
- `quicx_quic_packets_retransmit` - Packets retransmitted

#### HTTP/3
- `quicx_http3_requests_total` - Total HTTP/3 requests
- `quicx_http3_requests_active` - Active HTTP/3 requests

#### Performance
- `quicx_rtt_smoothed_us` - Smoothed RTT (microseconds)
- `quicx_congestion_window_bytes` - Congestion window size

## Contribution

Issues and Pull Requests are welcome!
