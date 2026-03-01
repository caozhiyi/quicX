# Configuration Reference

In `quicX`, the configuration system is meticulously divided into two layers: **QUIC Basic Transport Layer Configuration** and **HTTP/3 Application Layer Configuration**. This design allows you to exert extremely fine-grained control over network behavior according to your needs.

This document completely lists the configuration instructions for both layers, along with their default values and applicable scenarios. Whether you want to squeeze out ultimate throughput or reduce idle resource consumption of long connections, you will find the answers here.

---

## 1. QUIC Transport Layer Configuration

If you use pure QUIC interfaces, you directly manipulate the following structs. If you use HTTP/3 interfaces, the following structs are nested within `Http3Config::quic_config_` and `Http3Settings`.

### 1.1 `QuicConfig`: Global Runtime and Security Mechanisms

It primarily determines "global" behaviors such as event loops, encryption/decryption, and logging.

| Field Name / Type | Default Value | Global Significance and Tuning Suggestions |
| :--- | :--- | :--- |
| `thread_mode_`<br>`ThreadMode` | `kSingleThread` | **Core**: Controls the engine's multi-threading architecture.<br/>- `kSingleThread`: Ultimate single-core low latency, void of lock switching overhead.<br/>- `kMultiThread`: Suitable for high-concurrency servers on modern multi-core CPUs. Underlying UDP packets are hash-distributed to different Workers. |
| `worker_thread_num_`<br>`uint16_t` | `2` | When the mode is `kMultiThread`, how many Worker threads to start. Recommended to set to `CPU Cores - 1`, leaving one core for the OS to schedule network interrupts. |
| `log_level_`<br>`LogLevel` | `kNull` | Log level control. Silenced by default for maximum performance. Can be set to `kInfo` or `kDebug` for debugging. |
| `quic_version_`<br>`uint32_t` | `kQuicVersion2` | Preferred protocol version for negotiation. Defaults directly to the latest QUIC v2 (RFC 9369). You can manually downgrade to v1. |
| `enable_0rtt_`<br>`bool` | `false` | **Performance**: When enabled, if the client has connected to the server before and holds a ticket, it can send the first HTTP request **before the handshake completes**! Saves 1-RTT latency, extremely suitable for connection-unaware APIs (e.g., REST interfaces). |
| `keylog_file_`<br>`std::string` | `""` | **Extremely Important Debug Feature**: When enabled (by passing a file path), `quicX` will dump the TLS encryption keys for each client into this log file. Can be combined with Wireshark for plaintext decryption and flow tracing. |

### 1.2 `QlogConfig`: Network Tracing and Diagnostic Analysis
Within `QuicConfig`, `qlog_config_` is an extremely important kernel diagnostic switch. When enabled, the program outputs the data flow of every frame as structured logs according to the RFC 9001 specification (compatible with frontend visual tools like `qvis` and `Wireshark`).

| Field Name / Type | Default Value | Functional Significance and Tuning Suggestions |
| :--- | :--- | :--- |
| `enabled` | `false` | Whether to enable qlog collection. Because this severely impacts ultimate throughput, it is recommended only temporarily when resolving packet loss or algorithm behavior bugs. |
| `output_dir` | `"./qlogs"` | Root directory where log files output. |
| `format` | `kSequential` | File syntax formats. Defaults to `kSequential` (JSON Line mode) to support streamed writing and prevent memory exhaust. |
| `batch_write` | `true` | Disk asynchronous batch. **Must remain ON** under production load testing during diagnostic logging preventing I/O loops blocking Event calls. |
| `flush_interval_ms` | `100` | Once `batch_write` applies, flushes logs pool down the memory into hard-disk drives across this defined ms threshold. |
| `max_file_size_mb` | `100` | The single maximum capacity for Qlog dump chunks. After surpassing this boundary auto-rotates split new files (prevents disk explosion). |
| `max_file_count` | `10` | The historic total file preservation boundaries, automatically dropping rolling data beyond this point. |

### 1.3 `QuicServerConfig` Extension: Anti-DDoS and Retry Mechanisms
If you are instantiating a Server (`IQuicServer` or `quicx::IServer`), you configure `quicX`'s defensive moat through `QuicServerConfig`. Based on the RFC 9000 specification, `Retry` packets are designed to defend against source address spoofing UDP amplification attacks.

| Field Name | Default Value | Functional Significance and Tuning Suggestions |
| :--- | :--- | :--- |
| `retry_policy_` | `SELECTIVE` | **Defense Strategy**:<br/>- `NEVER`: Never send Retries (best performance, suitable for purely trusted internal environments).<br/>- `SELECTIVE`: (Recommended) Dynamically enables anti-spoofing verification when a sudden spike in new connection frequency or frequent requests from a specific IP are detected.<br/>- `ALWAYS`: Always requires any connecting client to undergo an extra round of handshake verification (most secure but adds 1-RTT latency). |
| `retry_token_lifetime_` | `60` (seconds) | The validity time of the token dispatched to the client to prove "it really is this IP". |
| `selective_retry_config_.rate_threshold_` | `1000` | (Active only in `SELECTIVE` mode) Global new connection rate threshold (connections/second). If the system receives an excessive number of new connections within a second exceeding this rate, it globally dispatches Retries for scrubbing. |
| `selective_retry_config_.ip_rate_threshold_` | `100` | (Active only in `SELECTIVE` mode) Single IP rate threshold (connections/minute). IPs exceeding this rate are flagged and forced to verify authenticity individually. |

### 1.4 `QuicTransportParams`: Transport Parameters Negotiation Dictionary

These configurations are packaged during the TLS extension phase of the handshake and sent to the peer, primarily used to control **Sliding Windows (Flow Control)**.

> [!WARNING]
> You must understand QUIC's dual current-limiting concept. If you fail to enlarge these flow control settings, despite owning standard generic 10Gbps capacities, endpoints fall back toward kb-level constraints based purely on "Network dictates the Windows full." 

| Field Name | Default Value | Detailed Mechanism Explanation |
| :--- | :--- | :--- |
| **`max_idle_timeout_ms_`** | `120000` (2 minutes) | Exceeding this boundary without interactivity (App exchanges / PING) severs the connection abruptly. Useful tuning IoT devices. |
| **`initial_max_data_`** | `64 MB` | **Connection-level flow control**. Cumulatively, total transmitted volumes from all streams prior to enforcing a pause-lock anticipating the peer broadcasting `Window_Update`. **Crucial Threshold limit 1 on oversized downloads**. |
| **`initial_max_stream_data_bidi_local_`** | `16 MB` | **Local Originated Dual-Stream limiter**. The maximum chunk allocation isolated specifically onto particular Sub-Streams you fire. **Crucial Threshold limit 2**. |
| **`initial_max_streams_bidi_`** | `200` | Concurrent permitted parallel pathways originating opposite direction streams (Concurrency limit of API routing bounds over HTTP/3 representations). You likely seek 1000 limits deploying microservice traffic aggregates. |
| **`ack_delay_exponent_ms_`** | `3` | Multiplier defining RTT ping estimates calculating Delay. Unless engaging purely analytical academic inquiries, don't modify. |
| **`max_ack_delay_ms_`** | `25` | Dictating forced maximal window intervals postponing pending ACKs toward the opposite endpoint. Expanded limits save package count transfers but could trick sources towards invoking PTO parameters early asserting artificial Packet Loss events. |

### 1.5 `MigrationConfig`: Advanced Feature - Connection Migration

QUIC's signature feature, employed to achieve true "lossless mobile network switching."

| Field Name | Default Value | Functional Significance |
| :--- | :--- | :--- |
| `enable_active_migration_` | `true` | (Client) Active migration. When the client detects a local network adapter change, it proactively sends packets to the server using the original Connection_ID to validate the new path. |
| `enable_nat_rebinding_` | `true` | (Server) Passive mapping resurrection. When a client is in an internet cafe or using 4G/5G, and the operator's NAT port suddenly ages and changes, as long as the server verifies the packets are okay, it automatically refreshes the mapping without disconnecting. |
| `path_validation_timeout_ms_`| `6000` (6 seconds) | If the old path breaks and the new path's handshake probe (Path Challenge / Response) receives no response exceeding this time, it completely terminates. |

---

## 2. HTTP/3 Layer Specific Configuration

This section of HTTP/3 configuration resides within `Http3Config`, `Http3ServerConfig` and `Http3ClientConfig`. Because HTTP/3 requests themselves are built upon QUIC streams, its limitations are primarily focused on preventing the server from being overwhelmed by malicious requests.

### 2.1 Concurrency and Rate Limiting

| Field Name / Type | Default Value | Application Layer Tuning Suggestions |
| :--- | :--- | :--- |
| `max_concurrent_streams_`<br>`uint64_t` | `200` | **Extremely Core!** This represents the number of concurrent requests from a single client that an HTTP/3 server can handle simultaneously without having finished them. Once exceeded, newly initiated requests will be blocked. For microservice gateways or high-concurrency internal aggregation nodes, you might need to tune this above 1000. |
| `connection_timeout_ms_`<br>`uint32_t` | `0` (Forever) | (Client EXCLUSIVE) Asserts timeout restrictions whenever `IClient::DoRequest` triggers sending/establishing tasks spanning across milliseconds boundaries; reporting subsequent failures. Typically tailored bounds hit 3000~5000 parameters depending on generic public net qualities. |

### 2.2 HTTP/3 Exclusive Advanced Features

| Field Definition | Default Value | Function Parsing |
| :--- | :--- | :--- |
| `enable_push_` | `false` | Explores launching HTTP/3 Server Push mechanisms (RFC 9114). Activating grants servers powers via `Response::AppendPush` loading requested assets onto clients forcibly caches (Static contents, css). Generates possible drawbacks congesting active stream loads, strictly maintained as open alpha. |
| `qpack_max_table_capacity` | `0` (Dynamic table OFF) | Stored dynamically within `Http3Settings`. Dictates HTTP/3 header capacities dictionary volumes limit. Tuning boundaries toward 0 retains rigid System Static dictionaries (Zero Extra RAM Allocation, High Perf); whereas massive custom payload Headers (Traces, UUIDs, heavy localized cookies) greatly demand widening bounds yielding optimal internal allocations mitigating raw network consumption rates at the expense of footprint memories. |

### 2.3 Metrics Endpoint

The QUIC stack gathers very detailed core statistical intelligence (Drop ratios, Retries amounts, Buffering capacities). One might bind innate endpoint routing outputs exposing figures seamlessly.

```cpp
quicx::Http3ServerConfig server_config;
server_config.metrics_.prometheus_export = true;        // Enables Prometheus format exports
server_config.metrics_.prometheus_endpoint = "/metrics"; // Launches innate route listening /metrics inside quicX loops
```
After executing this composition, Operational monitoring software architectures patch into `/metrics` analyzing raw lifecycle health bar situations for the `quicX` module seamlessly.

---

## 3. Compile-Time Static Configuration (`config.h`)

Beyond Runtime dynamic parameters insertions modifying systems, prioritizing relentless extreme performance parameters aligning hard memories demands, `quicX` deploys bottom-layer critical limit boundaries strictly via C++ `constexpr` standard formatting logic variables. Those targets remain planted firmly among original Source-code directories.

Executing architectures atop highly rigorous restricted Embedded machines environments or massive Multi-10Gbps Server network ports implies requirements rewriting components **Prior to Compilation** actions applying changes:

### 3.1 HTTP/3 Compile-Time Parameters (`src/http3/config.h`)

| Constant Variable Title | Default Value | Parsing Significance |
| :--- | :--- | :--- |
| `kMaxDataFramePayload` | `1350` | Maximum singular HTTP/3 DATA Frame slicing dimension injected back down inside native lower-layer transmission processes. Precisely targeting Standard 1500 MTU values factoring UDP/IP/QUIC & H3 metadata Tag deductions ensuring perfectly aligned insertions. |
| `kServerPushWaitTimeMs` | `30000` | Timeouts (30 seconds) bounding client's maximum tolerances upon Server pushed logic delivery arrivals. |
| `kClientConnectionTimeoutMs` | `60000` | Passive idle disconnection timelines applying Client level HTTP/3 sessions (60 Sec limits). |

### 3.2 QUIC Layer Compile-Time Parameters (`src/quic/config.h`)

Holds innermost protocol control configuration structures mapping thresholds limiting mechanisms:

| Constant Variable Name | Default Value | Mechanism and Geek Tune Definitions |
| :--- | :--- | :--- |
| **Sliding Window Congestion Re-fill Limitations** | |
| `kDataBlockedThreshold` | `16384` (16KB) | Systematizes global sender windows issuing `DATA_BLOCKED` frame claims upon thresholds cascading past these bare margins actively requesting openings counterparts. |
| `kDataIncreaseThreshold` | `512 * 1024` (512KB) | Target endpoints witnessing their operational capacities sinking under these parameters proactively launches `MAX_DATA` responses alerting dispatchers "Room availability extended; Resume transmitting". Multiplying parameters prevents gigabit network stalling bottlenecks ensuring persistent flow rates. |
| `kDataIncreaseAmount` | `2 * 1024 * 1024` (2MB) | Exact volume quantities expanded globally across active sliding windows returning prior alert mechanisms. Dedicated bulk raw file server paths frequently mandate limits surpassing basic 10MB intervals. |
| `kStreamWindowIncrement` | `2 * 1024 * 1024` | Expanded quota bounds singularly mapping individually generated concurrent Streams limits. |
| **Memory Pools & Package Bound Defaults** | |
| `kMaxFramePayload` | `1420` | Native basic individual Payload limits enforcing standard Generic QUIC frames. |
| `kPacketPoolSize` | `256` | Anticipated **Pre-Allocated** Memory Packet sizes bound directly into fundamental Memory Pools. Suggested maintaining power of 2 formulas constraints. Gateway load-balancers heavily demanding runtime stabilizations removing sporadic allocation stutters profit considerably maximizing values reaching 1024/2048 margins. |
| `kPacketBufferSize` | `1500` | Native limits conforming entirely aligned alongside pure generic typical Ethernet transmission MTUs. Expanding limits invites cataclysmic impacts inflicting aggressive forced fragmentation IP penalties crushing entire architectural load bounds drastically. |
| **Protocol and Core Handshake Logic** | |
| `kHandshakeTimeoutMs` | `5000` (5 Seconds) | Absolute maximum threshold limitations capping slow TCP/TLS handshake strikes thwarting potential vulnerability attack exploits seamlessly. |
| `kDefaultTlsVerifyPeer` | `false` | **Warning**: Strict parameter restricting innate Peer Certificate Authentication Validation logic specifically defaulted bypassed (`false`) enabling rapid un-hindered localhost testbeds. Essential enforcing strict `true` booleans securing productions or over-writing runtime flags. |

When modifying above constants upon header files configurations, ALWAYS re-invoke compilation architectures via `cmake --build` implementing baseline engine system re-works!
