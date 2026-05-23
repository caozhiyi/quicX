# Configuration Reference

In `quicX`, configuration is split into two layers: the **QUIC transport layer** and the **HTTP/3 application layer**. This separation lets you tune network behaviour at a very fine granularity — whether your goal is to squeeze out maximum throughput or to minimise idle resource usage on long-lived connections.

This document lists every configuration option in both layers, their default values, and when to change them.

---

## 1. QUIC Transport Layer Configuration

If you use the pure QUIC API you operate directly on the structs below. If you use the HTTP/3 API the same structs are nested inside `Http3Config::quic_config_` and `Http3Settings`.

### 1.1 `QuicConfig`: Global Runtime and Security

This struct controls "global" behaviour: event loop, crypto, logging.

| Field / Type | Default | Meaning and tuning notes |
| :--- | :--- | :--- |
| `thread_mode_`<br>`ThreadMode` | `kSingleThread` | **Core**: Engine threading model.<br/>- `kSingleThread`: Lowest latency on a single core, no lock contention.<br/>- `kMultiThread`: For high-concurrency servers on multi-core CPUs. Incoming UDP packets are hashed across worker threads. |
| `worker_thread_num_`<br>`uint16_t` | `2` | Number of worker threads in `kMultiThread` mode. Recommended: `CPU cores - 1`, leaving one core for the OS to handle network interrupts. |
| `log_level_`<br>`LogLevel` | `kNull` | Log level. Silenced by default for maximum performance. Set to `kInfo` or `kDebug` while debugging. |
| `quic_version_`<br>`uint32_t` | `kQuicVersion2` | Preferred protocol version to negotiate. Defaults to QUIC v2 (RFC 9369). You can manually downgrade to v1. |
| `enable_0rtt_`<br>`bool` | `false` | **Performance**: When enabled, a returning client that holds a session ticket can send its first HTTP request **before the handshake completes**, saving 1 RTT. Ideal for stateless APIs (e.g. REST). |
| `keylog_file_`<br>`std::string` | `""` | **Critical debugging knob**: When set to a file path, `quicX` writes the per-connection TLS secrets to that file. Combined with Wireshark this lets you decrypt and inspect the wire traffic. |

### 1.2 `QlogConfig`: Network Tracing and Diagnostics

Inside `QuicConfig`, `qlog_config_` controls the in-kernel diagnostic tracer. When enabled, every frame is emitted as a structured log following RFC 9001 (consumable by `qvis` and Wireshark).

| Field / Type | Default | Meaning and tuning notes |
| :--- | :--- | :--- |
| `enabled` | `false` | Enable qlog collection. Because tracing has a noticeable throughput cost, only turn it on when chasing packet loss or congestion-control bugs. |
| `output_dir` | `"./qlogs"` | Root directory for log files. |
| `format` | `kSequential` | File format. Defaults to `kSequential` (JSON-Lines) so logs can be streamed to disk without buffering everything in memory. |
| `batch_write` | `true` | Asynchronous batched disk writes. **Keep this on** under production load — synchronous I/O would block the event loop. |
| `flush_interval_ms` | `100` | When `batch_write` is on, the buffered logs are flushed to disk every N milliseconds. |
| `max_file_size_mb` | `100` | Per-file size cap. Once exceeded, the file is rotated (so a runaway trace cannot fill the disk). |
| `max_file_count` | `10` | Maximum number of rolled files retained; older files are deleted. |

### 1.3 `QuicServerConfig`: Anti-DDoS and Retry

If you instantiate a server (`IQuicServer` or `quicx::IServer`), `QuicServerConfig` lets you configure quicX's defensive moat. Per RFC 9000, `Retry` packets exist to defend against UDP source-address spoofing and amplification attacks.

| Field | Default | Meaning and tuning notes |
| :--- | :--- | :--- |
| `retry_policy_` | `SELECTIVE` | **Defence policy**:<br/>- `NEVER`: Never send Retry (fastest; only safe in fully trusted internal environments).<br/>- `SELECTIVE` (recommended): Dynamically enables address validation when new-connection rate spikes or a single IP gets noisy.<br/>- `ALWAYS`: Force every connecting client to do an extra round-trip address validation (most secure, costs 1 RTT). |
| `retry_token_lifetime_` | `60` (seconds) | Validity of the Retry token issued to a client to prove "the source address really is yours". |
| `selective_retry_config_.rate_threshold_` | `1000` | (Used only in `SELECTIVE` mode) Global new-connection rate threshold (conn/sec). Once exceeded, Retry is enabled globally to scrub spoofed traffic. |
| `selective_retry_config_.ip_rate_threshold_` | `100` | (Used only in `SELECTIVE` mode) Per-IP rate threshold (conn/min). IPs above this rate are flagged and forced through Retry validation individually. |

### 1.4 `QuicTransportParams`: Negotiated Transport Parameters

These values are packed into the TLS extension during the handshake and sent to the peer. They primarily control **flow-control sliding windows**.

> [!WARNING]
> Make sure you understand QUIC's two-level flow control. If you do not raise these limits, even a 10 Gbps link will be capped at kilobyte-level throughput because the peer will keep telling you "the window is full".

| Field | Default | Detailed semantics |
| :--- | :--- | :--- |
| **`max_idle_timeout_ms_`** | `120000` (2 min) | If no traffic (application packets or PING) is exchanged for this long, the connection is torn down. Increase for IoT devices that only chat occasionally. |
| **`initial_max_data_`** | `64 MB` | **Connection-level flow control**. Maximum cumulative bytes that can be sent across all streams before the peer must issue a `MAX_DATA` window update. **Bottleneck #1 for large transfers.** |
| **`initial_max_stream_data_bidi_local_`** | `16 MB` | **Per-stream limit for locally-initiated bidirectional streams**. Maximum bytes you can send on one stream before needing a window update. **Bottleneck #2 for large transfers.** |
| **`initial_max_streams_bidi_`** | `200` | Maximum number of bidirectional streams the peer is allowed to open concurrently (in HTTP/3 this maps to concurrent requests). High-fan-in microservice gateways may need 1000+. |
| **`ack_delay_exponent_ms_`** | `3` | ACK-delay multiplier used in RTT calculations. Don't change unless you're doing protocol research. |
| **`max_ack_delay_ms_`** | `25` | Upper bound on how long a receiver may defer an ACK. Larger values save a few packets but may cause the sender to mistake the delay for loss and trigger PTO prematurely. |

### 1.5 `MigrationConfig`: Connection Migration

QUIC's signature feature, used to achieve true "lossless mobile network handover".

| Field | Default | Meaning |
| :--- | :--- | :--- |
| `enable_active_migration_` | `true` | (Client) Active migration. When the client detects a local NIC change, it actively probes the new path with the existing Connection ID. |
| `enable_nat_rebinding_` | `true` | (Server) Passive NAT rebinding. When a client's NAT mapping ages out (e.g. on 4G/5G or behind public Wi-Fi), the server transparently refreshes the mapping as long as the packets validate, instead of dropping the connection. |
| `path_validation_timeout_ms_` | `6000` (6 s) | If the new path's PATH_CHALLENGE / PATH_RESPONSE handshake does not complete within this time after the old path breaks, the connection is closed. |

---

## 2. HTTP/3 Layer Configuration

HTTP/3 settings live in `Http3Config`, `Http3ServerConfig`, and `Http3ClientConfig`. Because HTTP/3 requests ride on QUIC streams, these knobs are mostly about preventing the server from being overwhelmed by abusive clients.

### 2.1 Concurrency and Limits

| Field / Type | Default | Application-layer tuning notes |
| :--- | :--- | :--- |
| `max_concurrent_streams_`<br>`uint64_t` | `200` | **Very important.** The number of in-flight requests a single client may have outstanding against an HTTP/3 server. Once exceeded, additional requests are blocked. For microservice gateways or internal high-fan-in aggregators, push this above 1000. |
| `connection_timeout_ms_`<br>`uint32_t` | `0` (never) | (Client only) When `IClient::DoRequest` is dialing or sending and gets no response within this many milliseconds, the call fails. Typical public-internet values are 3000–5000. |

### 2.2 HTTP/3-Specific Advanced Features

| Field | Default | Meaning |
| :--- | :--- | :--- |
| `enable_push_` | `false` | Whether to enable HTTP/3 Server Push (RFC 9114). When on, the server can use `Response::AppendPush` to push static assets (e.g. `style.css`, large images) the client did not explicitly request. May cause head-of-line contention under heavy load — currently considered experimental. |
| `qpack_max_table_capacity` | `0` (dynamic table off) | Lives inside `Http3Settings`. Size of the QPACK header-compression dynamic table. `0` keeps QPACK in pure-static mode (no extra memory, best perf); raise it when your traffic carries large per-request headers (Trace IDs, large cookies) and you want to trade memory for bandwidth. |

### 2.3 Metrics Endpoint

The QUIC stack collects detailed counters (loss, retransmissions, buffer occupancy, …). You can expose them via a built-in HTTP/3 endpoint:

```cpp
quicx::Http3ServerConfig server_config;
server_config.metrics_.prometheus_export = true;        // export in Prometheus format
server_config.metrics_.prometheus_endpoint = "/metrics"; // serve metrics on /metrics inside the quicX server
```

After this is configured, your monitoring system can scrape `/metrics` directly to observe quicX runtime health.

---

## 3. Compile-Time Static Configuration (`config.h`)

In addition to runtime-tunable settings, a small set of low-level limits are baked in as C++ `constexpr` constants for performance and memory-alignment reasons. They live in the source tree and require a recompile to change.

If you need to deploy quicX on resource-constrained embedded devices, or on a top-end multi-10 GbE server, you may want to edit them **before building**:

### 3.1 HTTP/3 Compile-Time Constants (`src/http3/config.h`)

| Constant | Default | Meaning |
| :--- | :--- | :--- |
| `kMaxDataFramePayload` | `1350` | Maximum payload size of a single HTTP/3 DATA frame handed to the transport. `1350` is sized to fit a 1500-byte MTU after subtracting IP / UDP / QUIC / AEAD-tag / H3 framing overhead. |
| `kServerPushWaitTimeMs` | `30000` | How long (ms) the client will wait for a pushed stream from the server (30 s). |
| `kClientConnectionTimeoutMs` | `60000` | Idle timeout for the client-side HTTP/3 session (60 s). |

### 3.2 QUIC Compile-Time Constants (`src/quic/config.h`)

The protocol's core control-plane limits.

| Constant | Default | Meaning and tuning notes |
| :--- | :--- | :--- |
| **Sliding window / congestion replenishment** | |
| `kDataBlockedThreshold` | `16384` (16 KB) | When the sender's remaining global send-window drops below this, it sends a `DATA_BLOCKED` frame to the peer. |
| `kDataIncreaseThreshold` | `512 * 1024` (512 KB) | When the receiver's available window falls below this, it sends a `MAX_DATA` frame ("you can keep sending"). On 10 GbE links you should multiply this so the sender never stalls. |
| `kDataIncreaseAmount` | `2 * 1024 * 1024` (2 MB) | How much window credit a `MAX_DATA` update grants. Bulk file-transfer servers can safely raise this above 10 MB. |
| `kStreamWindowIncrement` | `2 * 1024 * 1024` | Per-stream window credit granted on a `MAX_STREAM_DATA` update. |
| **Memory pools and packet defaults** | |
| `kMaxFramePayload` | `1420` | Maximum payload of a single generic QUIC frame. |
| `kPacketPoolSize` | `256` | Number of pre-allocated packet buffers in the memory pool. Keep it a power of two. Gateway / load-balancer nodes benefit from raising it to 1024 / 2048 to remove allocation jitter. |
| `kPacketBufferSize` | `1500` | Packet buffer size; sized to a typical Ethernet MTU. **Do not raise this** — going above MTU will cause IP fragmentation and tank throughput. |
| **Handshake and protocol** | |
| `kHandshakeTimeoutMs` | `5000` (5 s) | Hard upper bound on TLS handshake duration; protects against slowloris-style attacks. |
| `kDefaultTlsVerifyPeer` | `false` | **Heads up**: TLS peer-certificate verification is disabled by default to make local testing painless. **Set this to `true`, or override at runtime, before deploying to production.** |

After editing any of these constants you must rerun `cmake --build` to rebuild the library — the changes only take effect after a recompile.
