<p align="left"><img width="500" src="./docs/image/logo.png" alt="quicX logo"></p>

<p align="left">
  <a href="https://opensource.org/licenses/BSD-3-Clause"><img src="https://img.shields.io/badge/license-BSD--3--Clause-orange.svg" alt="License"></a>
  <img src="https://img.shields.io/badge/version-0.1.0-blue.svg" alt="Version">
  <img src="https://img.shields.io/badge/status-pre--1.0%20preview-yellow.svg" alt="Status">
  <img src="https://img.shields.io/badge/C%2B%2B-17-blue.svg" alt="C++17">
  <img src="https://img.shields.io/badge/RFC-9000%20%2F%209369%20%2F%209114-informational.svg" alt="RFC">
</p>

[简体中文](./README_cn.md)

**QuicX** is a C++17 HTTP/3 library built on the QUIC protocol (RFC 9000 / RFC 9369). It provides a self-contained transport and application stack — from UDP sockets and TLS 1.3 (via BoringSSL) through QUIC streams all the way to HTTP/3 routing, QPACK header compression, and server push — without depending on any external HTTP framework.

> **Documentation:**
> [English docs index](./docs/en/README.md) ·
> [中文文档索引](./docs/zh/README.md) ·
> [`CHANGELOG`](./CHANGELOG.md) ·
> [`SECURITY`](./SECURITY.md) ·
> [`CONTRIBUTING`](./CONTRIBUTING.md)

---

## Project Maturity

QuicX **v0.1.x is a pre-1.0 preview release.** The protocol stack is feature-complete for QUIC v1 / v2 and HTTP/3, has a sizeable unit-test corpus (1196+ tests, all green), passes ASan / UBSan / TSan clean, and runs in interoperability simulations against the major QUIC implementations. It is suitable for:

- Internal services and prototypes
- Research, education, and protocol experimentation
- Greenfield products willing to track minor releases

It is **not yet recommended** for mission-critical production traffic without your own additional protocol / fuzz / stress validation. Specifically:

- **API stability**: the public C++ API may change between `0.x` minor versions. SemVer guarantees take effect from `1.0.0`. See [`docs/en/reference/api_stability.md`](./docs/en/reference/api_stability.md).
- **Feature scope**: see [`docs/en/reference/support_matrix.md`](./docs/en/reference/support_matrix.md) for a precise list of what is implemented, partially implemented, and explicitly not implemented (e.g. CID rotation during connection migration, Multipath QUIC, DATAGRAM frames).
- **Known issues** and breaking-change history: see [`CHANGELOG.md`](./CHANGELOG.md).
- **Security disclosures**: please follow [`SECURITY.md`](./SECURITY.md) — do **not** open public GitHub issues for vulnerabilities.

If you find QuicX useful, contributions are very welcome — see [`CONTRIBUTING.md`](./CONTRIBUTING.md).

---

## Table of Contents

- [Project Maturity](#project-maturity)
- [Features](#features)
- [Architecture](#architecture)
- [Interop Matrix](#interop-matrix)
- [Documentation](#documentation)
- [Examples](#examples)
- [Observability](#observability)
- [Testing](#testing)
- [License](#license)

---

## Features

> a self-contained C++17 HTTP/3 + QUIC v1 / v2 stack — TLS 1.3
> via BoringSSL, BBR / CUBIC / Reno congestion control, QPACK + server push,
> connection migration, key update, optional QLog tracing — and a rich
> built-in metrics registry, all behind two CMake imported targets.

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

## Interop Matrix

QuicX is continuously tested against the major QUIC implementations using the
[`quic-interop-runner`](https://github.com/quic-interop/quic-interop-runner)
test suite — **14 scenarios × 12 peers × 2 directions = 322 test cells per run**.

Latest snapshot (from most recent test run):

| Metric | Value |
|---|---|
| PASS | **222** |
| FAIL | **20** |
| - Unsupported | **94** |
| **Pass rate**（excluding Unsupported） | **91.7%** |

Per-implementation breakdown across 14 scenarios (format: Server results / Client results):

| Peer | Server (peer ⇐ QuicX-client) | Client (peer ⇒ QuicX-server) |
|---|:--:|:--:|
| **quicx**(self)   | 14/14 | 14/14 | **100%** |
| **picoquic**      | 13/14 | 12/13 | **96.2%** |
| **ngtcp2**        | 12/12 | 11/11 | **100%** |
| **quic-go**       | 10/10 |  9/9  | **100%** |
| **neqo**          | 10/10 |  9/10 | **95.0%** |
| **lsquic**        | 10/10 |  9/10 | **95.0%** |
| **aioquic**       | 10/10 |  9/10 | **95.0%** |
| **quiche**        |  7/7  |  8/8  | **100%** |
| **msquic**        |  8/10 | 10/10 | **90.0%** |
| **mvfst**         |  4/6  |  1/6  | **41.7%** |
| **quinn**         | 14/14 |  9/9  | **100%** |
| **s2n-quic**      |  0/6  |  9/10 | **56.3%** |

> Each cell shows PASS count out of 14 scenarios in that direction.
> See the full reports for detailed per-scenario analysis, failure root causes,
> and breakdown of upstream/environment issues vs. QuicX defects.

Scenarios covered: `handshake` · `transfer` · `retry` · `resumption` ·
`zerortt` · `http3` · `multiconnect` · `versionnegotiation` · `chacha20` ·
`keyupdate` · `v2` · `rebind-port` · `rebind-addr` · `connectionmigration`.

Full reports (per-scenario matrix, known issues, detailed analysis):
[English](./docs/en/reports/interop_status.md) ·
[中文 (more complete)](./docs/zh/reports/interop_status.md) ·
How to reproduce locally:
[`docs/en/guide/interop_runbook.md`](./docs/en/guide/interop_runbook.md)
/ [`docs/zh/guide/interop_runbook.md`](./docs/zh/guide/interop_runbook.md).

---

## Documentation

QuicX ships parallel English and Chinese documentation trees under
[`docs/`](./docs). Pick your language and use that as the entry point — the
language-local README is the canonical index, this top-level README does not
duplicate it:

- **English** → [`docs/en/README.md`](./docs/en/README.md)
- **中文** → [`docs/zh/README.md`](./docs/zh/README.md)

Each tree is organized as: `getting-started/` · `tutorial/` · `guide/` ·
`reference/` (release contract) · `reports/` (point-in-time results) ·
`design/` (current code invariants).

Repo-root contracts: [`CHANGELOG.md`](./CHANGELOG.md) ·
[`SECURITY.md`](./SECURITY.md) ·
[`CONTRIBUTING.md`](./CONTRIBUTING.md).

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
