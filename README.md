<p align="left"><img width="500" src="./docs/image/logo.png" alt="quicX logo"></p>

<p align="left">
  <a href="https://opensource.org/licenses/BSD-3-Clause"><img src="https://img.shields.io/badge/license-BSD--3--Clause-orange.svg" alt="License"></a>
</p>

[简体中文](./README_cn.md)

**QuicX** is a C++17 HTTP/3 library built on the QUIC protocol (RFC 9000 / RFC 9369). It provides a self-contained transport and application stack — from UDP sockets and TLS 1.3 (via BoringSSL) through QUIC streams all the way to HTTP/3 routing, QPACK header compression, and server push — without depending on any external HTTP framework.

---

## Table of Contents

- [Features](#features)
- [Architecture](#architecture)
- [Documentation & Tutorials](#documentation--tutorials)
- [Examples](#examples)
- [Observability](#observability)
- [Testing](#testing)
- [License](#license)

---

## Features

### QUIC Protocol (RFC 9000 / RFC 9369)

| Area | Details |
|---|---|
| **TLS** | TLS 1.3 via BoringSSL; 0-RTT / 1-RTT handshakes; session ticket caching; SSLKEYLOGFILE support |
| **Versions** | QUIC v1 (`0x00000001`) and QUIC v2 (`0x6b3343cf`) with version negotiation |
| **Connection** | Multi-connection management; graceful `CONNECTION_CLOSE`; Retry packet anti-amplification |
| **Connection Migration** | Active migration (RFC 9000 §9); NAT rebinding detection; path validation via `PATH_CHALLENGE` / `PATH_RESPONSE` |
| **Streams** | Bidirectional and unidirectional streams; stream-level and connection-level flow control; `STREAM_DATA_BLOCKED` / `DATA_BLOCKED` frames |
| **Congestion Control** | BBR v1 / v2 / v3, CUBIC, Reno — selectable per connection via factory; packet pacer |
| **Loss Recovery** | ACK-based loss detection; PTO (Probe Timeout); packet retransmission with encryption-level tracking |
| **ECN** | Optional ECN marking and handling |
| **Key Update** | Optional automatic key update |

### HTTP/3

| Area | Details |
|---|---|
| **QPACK** | Static table + dynamic table (RFC 9204); Huffman encoding/decoding |
| **Streams** | Request / response streams; server-push streams (optional); control streams; encoder/decoder streams |
| **Routing** | Path parameter matching (`:param`); wildcard routes (`*`); per-method handler registration |
| **Middleware** | Before / After middleware chain per HTTP method |
| **Handler modes** | **Complete mode** — entire body buffered before handler; **Streaming mode** — `IAsyncServerHandler` / `IAsyncClientHandler` receive body chunks as they arrive |
| **HTTP Methods** | GET, HEAD, POST, PUT, DELETE, CONNECT, OPTIONS, TRACE, PATCH |
| **Server Push** | `PUSH_PROMISE` frames; optional push acceptance/rejection callback on the client |
| **HTTP Upgrade** | HTTP/1.1 → HTTP/3 upgrade path (`src/upgrade`) |

### Core Infrastructure

| Component | Details |
|---|---|
| **Memory** | Custom slab allocator (`NormalAlloter`); pooled `BufferChunk` chain; near-zero-copy I/O path |
| **Networking** | Cross-platform UDP I/O (`linux/`, `macos/`, `windows/`); non-blocking event loop |
| **Threading** | Single-thread or multi-thread mode; configurable worker thread count |
| **Timers** | Hierarchical timer wheel for connection idle, PTO, and application timers |
| **Logging** | Levelled logging (Null / Debug / Info / Warn / Error); configurable output path |
| **QLog** | RFC 9001-compliant QLog tracing (optional, `-DQUICX_ENABLE_QLOG=ON`) |
| **Metrics** | Rich built-in metrics registry — UDP, QUIC, HTTP/3, congestion, memory, TLS, migration, retry, etc. |

---

## Architecture

```
┌─────────────────────────────────────────┐
│         Application / Example           │
├─────────────────────────────────────────┤
│   HTTP/3 Layer  (src/http3)             │
│   IClient / IServer  ←→  Router         │
│   QPACK  ·  Frames  ·  Push            │
├─────────────────────────────────────────┤
│   HTTP Upgrade  (src/upgrade)           │
├─────────────────────────────────────────┤
│   QUIC Layer  (src/quic)               │
│   Connection  ·  Stream  ·  Crypto      │
│   Congestion Control  ·  Loss Recovery  │
│   Packet / Frame codec                  │
├─────────────────────────────────────────┤
│   Common  (src/common)                  │
│   Buffer  ·  Alloter  ·  Network I/O   │
│   Timer  ·  Log  ·  Metrics  ·  QLog   │
└─────────────────────────────────────────┘
```

---

## Documentation & Tutorials

We provide comprehensive guides to help you integrate and master `quicX`. For detailed integrations, code snippets, and fine-tuning, please refer to the following documents:

### Getting Started
* [Build & Compilation Guide](./docs/en/getting-started/build.md) - Learn how to build `quicX` via CMake or Bazel and integrate it into your cross-platform projects.
* [Running Your First Program](./docs/en/getting-started/quick_start.md) - Run and understand the native HTTP/3 Server and Client `Hello World` example.

### Core API & Configuration
* [HTTP/3 Application Layer API Guide](./docs/en/tutorial/http3_api_guide.md) - The out-of-the-box guide covering Route dispatching, Middlewares, Async Data Streaming (for large files), and Server Push.
* [Core QUIC Transport Layer API Guide](./docs/en/tutorial/quic_api_guide.md) - Deep dive into customizing private RPCs or game tunnels using raw QUIC Engine, Connection, and Stream abstractions.
* [Configuration Reference](./docs/en/tutorial/configuration_reference.md) - Detailed dictionary of all core structs (`QuicConfig`, `Http3Config`) including limits, flow control windows, connection migration, Qlog, and anti-DDoS mechanisms.

---

## Examples

All examples live under `example/` and are built with `-DBUILD_EXAMPLES=ON`. 

| Example | Description |
|---|---|
| `hello_world` | Minimal GET request / response |
| `restful_api` | REST API with path parameters |
| `file_transfer` | Large-file upload / download with streaming handler |
| `streaming_api` | Chunked streaming response |
| `bidirectional_comm` | Bidirectional stream communication |
| `concurrent_requests` | Multiple parallel requests |
| `connection_lifecycle` | Connection events and graceful shutdown |
| `error_handling` | Protocol error handling patterns |
| `server_push` | HTTP/3 Server Push (`PUSH_PROMISE`) |
| `load_testing` | Simple load generation |
| `performance_benchmark` | Throughput / latency benchmark |
| `metrics_monitoring` | Reading built-in metrics at runtime |
| `qlog_integration` | Generating QLog traces for Wireshark / qvis |
| `upgrade_h3` | HTTP/1.1 → HTTP/3 upgrade |
| `quicx_curl` | curl-like command-line client |

---

## Observability

### Metrics

QuicX ships a built-in metrics registry covering:

- **UDP**: packets/bytes rx/tx, drops, send errors
- **QUIC connections**: active, total, handshake success/failure, duration
- **QUIC packets**: rx/tx, retransmit, lost, dropped, acked
- **QUIC streams**: active, created, closed, bytes rx/tx, RESET frames
- **Flow control**: blocked events
- **HTTP/3**: requests total/active/failed, duration histogram, push promises, status code buckets (2xx/3xx/4xx/5xx)
- **Congestion control**: window, events, slow-start exits, bytes in flight, pacing rate
- **Latency**: smoothed RTT, RTT variance, min RTT, packet processing time, ACK delay
- **Memory**: pool allocations, free/allocated blocks
- **TLS**: handshake duration, sessions resumed/cached
- **Connection migration**: total, failed
- **Retry**: packets sent, trigger reasons, token validation

Access metrics at runtime via `MetricsRegistry` or expose via the optional metrics HTTP endpoint in `Http3ServerConfig::metrics_` / `Http3ClientConfig::metrics_`.

### QLog

Enable with `-DQUICX_ENABLE_QLOG=ON` and configure the path in `QuicConfig::qlog_config_`. Produced traces are compatible with [qvis](https://qvis.quictools.info/) and Wireshark.

---

## Testing

```bash
# Unit tests
./build/bin/quicx_utest

# Integration tests (requires local server/client pair)
python3 run_tests.py

# Congestion-control simulator
./build/bin/cc_simulator

# Fuzz testing (requires Clang + libFuzzer)
cmake -B build_fuzz -DENABLE_FUZZING=ON -DCMAKE_CXX_COMPILER=clang++
cmake --build build_fuzz
```

---

## License

BSD 3-Clause License — see the [LICENSE](LICENSE) file for details.
