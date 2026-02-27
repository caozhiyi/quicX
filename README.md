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
- [Quick Start](#quick-start)
- [Build](#build)
- [Examples](#examples)
- [Configuration](#configuration)
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

## Quick Start

### Minimal HTTP/3 Server

```cpp
#include "http3/include/if_request.h"
#include "http3/include/if_response.h"
#include "http3/include/if_server.h"

int main() {
    auto server = quicx::IServer::Create();

    server->AddHandler(quicx::HttpMethod::kGet, "/hello",
        [](std::shared_ptr<quicx::IRequest> req,
           std::shared_ptr<quicx::IResponse> resp) {
            resp->AppendBody(std::string("hello world"));
            resp->SetStatusCode(200);
        });

    quicx::Http3ServerConfig config;
    config.quic_config_.cert_pem_ = /* PEM string */;
    config.quic_config_.key_pem_  = /* PEM string */;
    config.quic_config_.config_.worker_thread_num_ = 2;
    config.quic_config_.config_.log_level_ = quicx::LogLevel::kInfo;

    server->Init(config);
    server->Start("0.0.0.0", 7001);
    server->Join();
}
```

### Minimal HTTP/3 Client

```cpp
#include "http3/include/if_client.h"
#include "http3/include/if_response.h"

int main() {
    auto client = quicx::IClient::Create();

    quicx::Http3ClientConfig config;
    config.quic_config_.config_.worker_thread_num_ = 1;
    client->Init(config);

    auto request = quicx::IRequest::Create();
    client->DoRequest("https://127.0.0.1:7001/hello",
        quicx::HttpMethod::kGet, request,
        [](std::shared_ptr<quicx::IResponse> resp, uint32_t error) {
            if (error == 0)
                std::cout << resp->GetBodyAsString() << "\n";
        });

    // wait for response …
}
```

### Streaming (Async) Handler

```cpp
class FileUploadHandler : public quicx::IAsyncServerHandler {
public:
    void OnHeaders(std::shared_ptr<quicx::IRequest> req,
                   std::shared_ptr<quicx::IResponse> resp) override {
        file_ = fopen("upload.dat", "wb");
        resp->SetStatusCode(200);
    }
    void OnBodyChunk(const uint8_t* data, size_t len, bool is_last) override {
        if (file_) fwrite(data, 1, len, file_);
        if (is_last && file_) { fclose(file_); file_ = nullptr; }
    }
    void OnError(uint32_t error) override {
        if (file_) { fclose(file_); file_ = nullptr; }
    }
private:
    FILE* file_ = nullptr;
};

server->AddHandler(quicx::HttpMethod::kPost, "/upload",
                   std::make_shared<FileUploadHandler>());
```

---

## Build

### Prerequisites

| Requirement | Version |
|---|---|
| C++ compiler | C++17 or later (GCC / Clang / MSVC) |
| CMake | ≥ 3.16 |
| BoringSSL | Git submodule (`third/boringssl`) |
| Threads | POSIX threads / Windows threads |
| GTest | Optional — for unit tests (auto-fetched) |

### Steps

```bash
# Clone with submodules (BoringSSL is a submodule)
git clone --recurse-submodules https://github.com/caozhiyi/quicX.git
cd quicX

# Configure
cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_EXAMPLES=ON \
    -DENABLE_TESTING=ON \
    -DQUICX_ENABLE_QLOG=ON

# Build
cmake --build build --parallel $(nproc)

# Run unit tests
./build/bin/quicx_utest
```

### CMake Options

| Option | Default | Description |
|---|---|---|
| `BUILD_EXAMPLES` | `ON` | Build all example programs |
| `ENABLE_TESTING` | `ON` | Build unit tests (GTest) |
| `ENABLE_BENCHMARKS` | `ON` | Build benchmark suite |
| `ENABLE_CC_SIMULATOR` | `ON` | Build congestion control simulator |
| `ENABLE_INTERGRATION` | `ON` | Build integration tests |
| `ENABLE_FUZZING` | `OFF` | Build libFuzzer fuzz targets |
| `ENABLE_INTEROP` | `OFF` | Build QUIC Interop Runner targets |
| `QUICX_ENABLE_QLOG` | `ON` | Enable QLog protocol tracing |

### Platforms

The CI matrix tests the following configurations on every push:

| OS | Compiler |
|---|---|
| Ubuntu (latest) | GCC, Clang |
| Windows (latest) | MSVC (`cl`) |
| macOS (latest) | Clang |

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

## Configuration

### Key `QuicConfig` fields

```cpp
quicx::QuicConfig cfg;
cfg.thread_mode_       = quicx::ThreadMode::kMultiThread;
cfg.worker_thread_num_ = 4;
cfg.log_level_         = quicx::LogLevel::kInfo;
cfg.log_path_          = "./logs";
cfg.enable_0rtt_       = true;   // 0-RTT session resumption
cfg.enable_ecn_        = false;  // ECN support
cfg.enable_key_update_ = false;  // automatic key update
cfg.quic_version_      = quic::kQuicVersion2;  // QUIC v2 preferred
cfg.keylog_file_       = "./tls_keys.log";     // Wireshark key log
```

### Transport Parameters (`QuicTransportParams`)

```cpp
quicx::QuicTransportParams tp;
tp.max_idle_timeout_ms_              = 120000;        // 2 minutes
tp.max_udp_payload_size_             = 1472;          // 1500 - 28
tp.initial_max_data_                 = 64*1024*1024;  // 64 MB
tp.initial_max_stream_data_bidi_local_  = 16*1024*1024;
tp.initial_max_stream_data_bidi_remote_ = 16*1024*1024;
tp.initial_max_streams_bidi_         = 200;
tp.disable_active_migration_         = false;
```

### Connection Migration

```cpp
quicx::MigrationConfig mc;
mc.enable_active_migration_     = true;
mc.path_validation_timeout_ms_  = 6000;
mc.max_probe_retries_           = 5;
```

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
