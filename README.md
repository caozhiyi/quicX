<p align="left"><img width="500" src="./docs/image/logo.png" alt="quicX logo"></p>

<p align="left">
  <a href="https://opensource.org/licenses/BSD-3-Clause"><img src="https://img.shields.io/badge/license-BSD--3--Clause-orange.svg" alt="License"></a>
  <img src="https://img.shields.io/badge/version-1.0.0-blue.svg" alt="Version">
  <img src="https://img.shields.io/badge/status-stable--v1.0.0-brightgreen.svg" alt="Status">
  <img src="https://img.shields.io/badge/interop-24%20scenarios%20%C3%97%2017%20peers%20%7C%2091.22%25-brightgreen.svg" alt="Interop">
  <img src="https://img.shields.io/badge/C%2B%2B-17-blue.svg" alt="C++17">
  <img src="https://img.shields.io/badge/RFC-9000%20%2F%209369%20%2F%209114-informational.svg" alt="RFC">
</p>

[简体中文](./README_cn.md)
---

**QuicX** is a self-contained C++17 QUIC / HTTP/3 protocol stack: from UDP socket, TLS 1.3 (BoringSSL), QUIC stream, all the way to HTTP/3 routing, QPACK, and server push, all implemented in a single repository without depending on any external HTTP framework.

It is built for **production services**: 1527 unit and integration tests, clean ASan / UBSan / TSan runs, and a 91.22% pass rate across a 24-scenario × 17-peer interop matrix — the complete packet lifecycle, from the network card to the HTTP/3 handler, holds up under scrutiny.

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

QuicX is continuously tested against major QUIC implementations using [`quic-interop-runner`](https://github.com/quic-interop/quic-interop-runner) — **24 scenarios × 17 peers across both directions = 744 test combinations per round** (22 RFC-conformance scenarios plus goodput / crosstraffic measurements). Most recent run (2026-09-04):

| Metric | Value |
|---|---|
| Pass | **592** |
| Fail | **57** |
| Unsupported | 95 |
| **Pass Rate** (excluding unsupported) | **91.22%** |
| Goodput / Crosstraffic | 30/30 / 28/30 |

By peer implementation, sorted by overall effective pass rate (`N/A` = the peer does not ship that role; `chrome` is HTTP/3-client-only):

| Peer | QuicX as Server | QuicX as Client | Pass Rate |
|---|:--:|:--:|:--:|
| **lsquic**   | 23/23 | 23/24 | 97.9% |
| **aioquic**  | 22/23 | 21/22 | 95.6% |
| **neqo**     | 21/24 | 24/24 | 93.8% |
| **picoquic** | 22/24 | 23/24 | 93.8% |
| **kwik**     | 22/23 | 20/22 | 93.3% |
| **xquic**    | 20/22 | 20/21 | 93.0% |
| **s2n-quic** | 19/20 | 20/22 | 92.9% |
| **ngtcp2**   | 23/24 | 21/24 | 91.7% |
| **quinn**    | 20/24 | 22/22 | 91.3% |
| **haproxy**  |  N/A  | 20/22 | 90.9% |
| **msquic**   | 18/22 | 21/21 | 90.7% |
| **nginx**    |  N/A  | 19/21 | 90.5% |
| **go-x-net** | 14/15 | 12/14 | 89.7% |
| **quic-go**  | 19/22 | 19/21 | 88.4% |
| **quiche**   | 17/20 | 19/21 | 87.8% |
| **mvfst**    | 12/18 | 15/19 | 73.0% |
| **chrome**   |  1/1  |  N/A  | 100%  |

Covered scenarios: `handshake`, `transfer`, `longrtt`, `chacha20`, `multiplexing`, `retry`, `resumption`, `zerortt`, `http3`, `blackhole`, `keyupdate`, `ecn`, `amplificationlimit`, `handshakeloss`, `transferloss`, `handshakecorruption`, `transfercorruption`, `ipv6`, `v2`, `rebind-port`, `rebind-addr`, `connectionmigration`, `goodput`, `crosstraffic`.

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
