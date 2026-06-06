<p align="left"><img width="500" src="./docs/image/logo.png" alt="quicX logo"></p>

<p align="left">
  <a href="https://opensource.org/licenses/BSD-3-Clause"><img src="https://img.shields.io/badge/license-BSD--3--Clause-orange.svg" alt="License"></a>
  <img src="https://img.shields.io/badge/version-1.0.0-blue.svg" alt="Version">
  <img src="https://img.shields.io/badge/status-learning--reference-brightgreen.svg" alt="Status">
  <img src="https://img.shields.io/badge/C%2B%2B-17-blue.svg" alt="C++17">
  <img src="https://img.shields.io/badge/RFC-9000%20%2F%209369%20%2F%209114-informational.svg" alt="RFC">
</p>

[简体中文](./README_cn.md)
---

**QuicX** is a self-contained C++17 QUIC / HTTP/3 protocol stack: from UDP socket, TLS 1.3 (BoringSSL), QUIC stream, all the way to HTTP/3 routing, QPACK, and server push, all implemented in a single repository without depending on any external HTTP framework.

Its goal is to be **readable and runnable** — a clear codebase showing you the complete lifecycle of a packet from the network card to the HTTP/3 handler, backed by 1196+ unit tests, clean ASan / UBSan / TSan runs, and a 91.7% interop pass rate with major implementations. The **only thing it hasn't done yet** is carry large-scale production traffic; please treat it as a trusted reference implementation to use, learn, and extend.

---

## Architecture

QuicX uses a **single-process architecture**: all five source modules — Application, HTTP/3, HTTP Upgrade, QUIC, and Common — are linked into the same address space.

<p align="center">
  <img src="./docs/image/architecture.svg" alt="QuicX module layout" width="900">
</p>

Requests flow top-to-bottom along the solid lines: user's `IServer` handler → HTTP/3 Connection / QPACK / Router → QUIC Stream and Congestion Control → Common networking and buffer → UDP out of network card.

**HTTP Upgrade sits at the same level as HTTP/3**: it opens a second listener on TCP 80/443 whose only responsibility is to write back an `Alt-Svc` response header; it **does not depend** on the QUIC layer and does not run HTTP/3 over TCP. The dashed line represents a **client-side hop**, not an in-process call — after receiving the header, the browser reconnects using UDP/QUIC.

> To see the complete path and invariants of a single packet from UDP to the HTTP/3 handler, see [`packet lifecycle`](./docs/en/design/packet_lifecycle.md).

---

## Features

### QUIC (RFC 9000 / RFC 9369)

| Feature Area | Description |
|---|---|
| **TLS** | TLS 1.3 via BoringSSL; 0-RTT / 1-RTT; session ticket caching; SSLKEYLOGFILE |
| **Protocol Version** | QUIC v1 (`0x00000001`) and v2 (`0x6b3343cf`), supporting version negotiation |
| **Connection** | Multi-connection management; graceful `CONNECTION_CLOSE`; Retry packet anti-amplification |
| **Connection Migration** | Active migration (§9); NAT rebinding detection; `PATH_CHALLENGE` / `PATH_RESPONSE` path validation |
| **Stream** | Bidirectional / unidirectional streams; stream-level + connection-level flow control |
| **Congestion Control** | BBR v1/v2/v3, CUBIC, Reno (selectable per connection factory); built-in packet pacer |
| **Loss Recovery** | ACK-based loss detection; PTO; retransmission tracking per encryption level |
| **Other** | Optional ECN; optional automatic key update |

### HTTP/3

| Feature Area | Description |
|---|---|
| **QPACK** | Static + dynamic tables (RFC 9204); Huffman encoding/decoding |
| **Stream** | Request / response, control, encoder / decoder, optional server push streams |
| **Routing** | Path parameter (`:param`), wildcard (`*`), registration by method |
| **Middleware** | Before / After chain per HTTP method |
| **Handler Mode** | **Complete mode** (buffers complete body) / **Streaming mode** (`IAsyncServerHandler` / `IAsyncClientHandler` receives in chunks) |
| **HTTP Methods** | GET, HEAD, POST, PUT, DELETE, CONNECT, OPTIONS, TRACE, PATCH |
| **Server Push** | `PUSH_PROMISE`; client configurable accept / reject callbacks |
| **HTTP Upgrade** | HTTP/1.1 → HTTP/3 upgrade path (`src/upgrade`) |

### Core Infrastructure

| Component | Description |
|---|---|
| **Memory** | Slab allocator (`NormalAlloter`); pooled `BufferChunk` chain; near-zero-copy I/O |
| **Network** | Cross-platform UDP I/O (Linux / macOS / Windows); non-blocking event loop |
| **Thread** | Single-threaded or multi-threaded; configurable worker count |
| **Timer** | Hierarchical timer wheel (connection idle, PTO, application timers) |
| **Logging & QLog** | Levelled logging; optional RFC 9001 QLog tracing (`-DQUICX_ENABLE_QLOG=ON`) |
| **Metrics** | Built-in Metrics registry, covering UDP / QUIC / HTTP/3 / Congestion / Memory / TLS / Migration / Retry |

> For the list of implemented / partially implemented features, see [`support matrix`](./docs/en/reference/support_matrix.md).

---

## Interop Testing

QuicX is continuously tested against major QUIC implementations using [`quic-interop-runner`](https://github.com/quic-interop/quic-interop-runner) — **14 scenarios × 12 peers × 2 directions = 322 test combinations per round**. Most recent run:

| Metric | Value |
|---|---|
| Pass | **222** |
| Fail | **20** |
| Unsupported | 94 |
| **Pass Rate** (excluding unsupported) | **91.7%** |

By peer implementation (Pass / Valid runs, less than 14 means certain scenarios were marked unsupported by either end):

| Peer | QuicX as Server | QuicX as Client | Pass Rate |
|---|:--:|:--:|:--:|
| **picoquic** | 13/14 | 12/13 | 96.2% |
| **ngtcp2**   | 12/12 | 11/11 | 100% |
| **quic-go**  | 10/10 |  9/9  | 100% |
| **neqo**     | 10/10 |  9/10 | 95.0% |
| **lsquic**   | 10/10 |  9/10 | 95.0% |
| **aioquic**  | 10/10 |  9/10 | 95.0% |
| **quiche**   |  7/7  |  8/8  | 100% |
| **msquic**   |  8/10 | 10/10 | 90.0% |
| **quinn**    | 14/14 |  9/9  | 100% |
| **mvfst**    |  4/6  |  1/6  | 41.7% |
| **s2n-quic** |  0/6  |  9/10 | 56.3% |

Covered scenarios: `handshake`, `transfer`, `retry`, `resumption`, `zerortt`, `http3`, `multiconnect`, `versionnegotiation`, `chacha20`, `keyupdate`, `v2`, `rebind-port`, `rebind-addr`, `connectionmigration`.

Full reports and root cause analysis: [`interop status`](./docs/en/reports/interop_status.md)
Local reproduction: [`interop runbook`](./docs/en/guide/interop_runbook.md).

---

## Getting Started

All examples are located in `example/`, enable `-DBUILD_EXAMPLES=ON` to compile. We recommend starting with `hello_world`; pick by scenario:

- **Request / Response Basics**: `hello_world`, `restful_api`, `error_handling`
- **Streaming & Large Files**: `streaming_api`, `file_transfer`, `bidirectional_comm`
- **Connection Behavior**: `connection_lifecycle`, `concurrent_requests`, `server_push`
- **Ops & Observability**: `metrics_monitoring`, `qlog_integration`, `performance_benchmark`, `load_testing`
- **Protocol Upgrade & Tools**: `upgrade_h3`, `quicx_curl` (a curl-like command-line client)

### Testing

```bash
# Unit tests
./build/bin/quicx_utest

# Integration tests (requires local server / client)
python3 run_tests.py

# Congestion control simulator
./build/bin/cc_simulator

# Fuzz testing (requires Clang + libFuzzer)
cmake -B build_fuzz -DENABLE_FUZZING=ON -DCMAKE_CXX_COMPILER=clang++
cmake --build build_fuzz
```

---

## Observability

**Metrics** — Built-in `MetricsRegistry`, covering UDP rx/tx and drop, QUIC connection / packet / stream, flow control blocking, HTTP/3 requests and status code buckets, congestion window and pacing, RTT and ACK delay, memory pool, TLS handshake and session resumption, connection migration, Retry and other dimensions. You can read `MetricsRegistry` directly at runtime, or expose an HTTP endpoint via `Http3ServerConfig::metrics_` / `Http3ClientConfig::metrics_`.

**QLog** — Enable by compiling with `-DQUICX_ENABLE_QLOG=ON` and configuring the output path in `QuicConfig::qlog_config_`. The generated trace files are compatible with [qvis](https://qvis.quictools.info/) and Wireshark.

---

## Further Reading

- English documentation entry: [`README`](./docs/en/README.md) (`getting-started/` · `tutorial/` · `guide/` · `reference/` · `reports/` · `design/`)
- Change history: [`CHANGELOG.md`](./CHANGELOG.md)
- Security disclosures: [`SECURITY.md`](./SECURITY.md)
- Contribution guide: [`CONTRIBUTING.md`](./CONTRIBUTING.md)

---

## License

BSD 3-Clause License — see [LICENSE](LICENSE) for details.
