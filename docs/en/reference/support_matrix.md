# QuicX Support Matrix

> Applies to **v0.1.x**. This document is the source of truth for "what works,
> what is partial, and what is intentionally not implemented" in the current
> release line. It is updated on every minor release.
>
> See also: [`CHANGELOG.md`](../../../CHANGELOG.md), [`api_stability.md`](./api_stability.md),
> [`reports/interop_status.md`](../reports/interop_status.md).

---

## Legend

| Symbol | Meaning |
|:---:|---|
| ✅ | Fully implemented, covered by unit tests, used in examples |
| 🟡 | Partially implemented — works in the common path; see notes for caveats |
| 🧪 | Implemented but considered experimental — API or behavior may change |
| ❌ | Not implemented in this release |
| 🚫 | Out of scope for this project |

---

## QUIC Transport (RFC 9000 / RFC 9369)

### Versions and negotiation

| Feature | Status | Notes |
|---|:---:|---|
| QUIC v1 (`0x00000001`, RFC 9000) | ✅ | Default |
| QUIC v2 (`0x6b3343cf`, RFC 9369) | ✅ | Selectable per connection |
| Version negotiation packet | ✅ | Server side |
| Forced version downgrade resistance | ✅ | Per RFC 9000 §6 |

### Handshake and TLS

| Feature | Status | Notes |
|---|:---:|---|
| TLS 1.3 via BoringSSL | ✅ | |
| 1-RTT handshake | ✅ | |
| 0-RTT handshake (early data) | ✅ | Replay protection per RFC 9001 §9 |
| Session ticket caching | ✅ | In-memory; persistence is the application's responsibility |
| `SSLKEYLOGFILE` for Wireshark | ✅ | |
| Retry packet (anti-amplification) | ✅ | `RetryPolicy::NEVER / SELECTIVE / ALWAYS` |
| Address validation token | ✅ | Including stateless retry token |
| Certificate verification | ✅ | Server cert verification on the client |
| Custom verifier callback | ✅ | Via `QuicClientConfig` |
| Client certificate (mTLS) | 🟡 | Code paths exist, lightly tested in unit tests, no end-to-end example |

### Connection lifecycle

| Feature | Status | Notes |
|---|:---:|---|
| Multi-connection management on a single UDP socket | ✅ | |
| Graceful `CONNECTION_CLOSE` (transport + application) | ✅ | |
| Stateless reset | ✅ | |
| Idle timeout | ✅ | Negotiated via transport parameters |
| `PING` frame for keep-alive | ✅ | |

### Streams and flow control

| Feature | Status | Notes |
|---|:---:|---|
| Bidirectional streams | ✅ | |
| Unidirectional streams | ✅ | |
| Stream-level flow control | ✅ | |
| Connection-level flow control | ✅ | |
| `STREAM_DATA_BLOCKED` / `DATA_BLOCKED` | ✅ | Emitted and handled |
| `MAX_STREAMS` and `STREAMS_BLOCKED` | ✅ | |
| `RESET_STREAM` / `STOP_SENDING` | ✅ | |

### Congestion control and loss recovery

| Feature | Status | Notes |
|---|:---:|---|
| BBR v1 | ✅ | |
| BBR v2 | ✅ | |
| BBR v3 | 🧪 | Implemented; tuning is preliminary, expect changes |
| CUBIC | ✅ | |
| Reno | ✅ | |
| Pluggable congestion controller (factory) | ✅ | Per-connection selection |
| Packet pacing | ✅ | |
| ACK-based loss detection (RFC 9002) | ✅ | |
| PTO (Probe Timeout) | ✅ | |
| Per-encryption-level retransmission tracking | ✅ | |
| ECN marking and feedback | 🟡 | Optional, off by default; not all simulator peers exercise this |

### Connection migration (RFC 9000 §9)

| Feature | Status | Notes |
|---|:---:|---|
| Active client migration | ✅ | |
| NAT rebinding detection | ✅ | |
| Path validation (`PATH_CHALLENGE` / `PATH_RESPONSE`) | ✅ | |
| **Connection ID rotation during migration** | ✅ | RFC 9000 §9.5: after a successful migration the local DCID is rotated to the next remote CID and the peer is asked to retire the old one; locally we honour §19.16 single-seq retire and §19.15 batch retire (`retire_prior_to`), then auto-replenish the local pool. Verified by interop self-test (connectionmigration / rebind-port / rebind-addr all PASS). |
| Server-initiated `NEW_CONNECTION_ID` rotation | ✅ | Issued during handshake; auto-replenished after migration / retire to keep `active_connection_id_limit` saturated |
| `RETIRE_CONNECTION_ID` handling | ✅ | |

### Other transport features

| Feature | Status | Notes |
|---|:---:|---|
| Key update (RFC 9001 §6) | ✅ | Optional, automatic |
| `HANDSHAKE_DONE` frame | ✅ | |
| `NEW_TOKEN` frame | ✅ | |
| Multipath QUIC (`draft-ietf-quic-multipath`) | ❌ | Not implemented |
| DATAGRAM frame (RFC 9221) | ❌ | Not implemented |
| ACK Frequency extension (`draft-ietf-quic-ack-frequency`) | ❌ | Not implemented |
| Reliable stream reset (`draft-ietf-quic-reliable-stream-reset`) | ❌ | Not implemented |
| Greasing (RFC 8701) | 🟡 | Frame type greasing yes; transport parameter greasing partial |

---

## HTTP/3 (RFC 9114)

### Core protocol

| Feature | Status | Notes |
|---|:---:|---|
| HTTP/3 framing (DATA / HEADERS / SETTINGS / GOAWAY / etc.) | ✅ | |
| Request streams | ✅ | |
| Response streams | ✅ | |
| Control stream | ✅ | |
| `GOAWAY` graceful shutdown | ✅ | |
| `SETTINGS` exchange | ✅ | |
| Reserved stream type ignoring | ✅ | |

### QPACK (RFC 9204)

| Feature | Status | Notes |
|---|:---:|---|
| Static table | ✅ | |
| Dynamic table (encoder + decoder) | ✅ | |
| Huffman encoding / decoding | ✅ | |
| Encoder stream | ✅ | |
| Decoder stream | ✅ | |
| Insert count and stream blocking | ✅ | |

### Server features

| Feature | Status | Notes |
|---|:---:|---|
| Path parameter routing (`/users/:id`) | ✅ | |
| Wildcard routing (`/static/*`) | ✅ | |
| Per-method handler registration | ✅ | All standard verbs |
| Before / After middleware chains | ✅ | Per-method |
| Server push (`PUSH_PROMISE`) | ✅ | |
| Streaming request body via `IAsyncServerHandler` | ✅ | |
| Trailers | 🟡 | Encoder support exists; routing-level convenience API is minimal |
| 1xx informational responses (`Early Hints`) | ❌ | |

### Client features

| Feature | Status | Notes |
|---|:---:|---|
| Synchronous-style request / response | ✅ | |
| Streaming response body via `IAsyncClientHandler` | ✅ | |
| Push promise accept / reject callback | ✅ | |
| Connection pooling across hosts | 🟡 | Per-host single-connection reuse; multi-host pool is application-side |

### Methods

| Method | Status |
|---|:---:|
| GET / HEAD / POST / PUT / DELETE | ✅ |
| OPTIONS / TRACE / PATCH | ✅ |
| CONNECT | ✅ |
| `CONNECT-UDP` (RFC 9298 / MASQUE) | ❌ |
| Extended CONNECT for WebTransport | ❌ |

---

## HTTP Upgrade

| Feature | Status | Notes |
|---|:---:|---|
| HTTP/1.1 → HTTP/3 `Alt-Svc` advertisement | ✅ | `src/upgrade` |
| Negotiation example | ✅ | `example/upgrade_h3` |
| HTTP/2 → HTTP/3 fallback | 🚫 | Out of scope (no HTTP/2 server in QuicX) |

---

## Observability

| Feature | Status | Notes |
|---|:---:|---|
| Built-in metrics registry | ✅ | UDP / QUIC / HTTP/3 / congestion / TLS / migration / retry / memory |
| Metrics HTTP endpoint | ✅ | Optional, configured via `Http3ServerConfig::metrics_` |
| QLog (RFC 9001 §A) | ✅ | Build with `-DQUICX_ENABLE_QLOG=ON` |
| Levelled logging | ✅ | |
| OpenTelemetry export | ❌ | Application can bridge from the metrics registry |

---

## Platforms

> Platform support is what the **build system targets**. Routine CI for all
> three platforms is on the v0.2.0 roadmap; today the matrix is "developer
> validated, no continuous coverage".

| Platform | Build | Runtime | Notes |
|---|:---:|:---:|---|
| Linux x86_64 (gcc 9+, clang 10+) | ✅ | ✅ | Primary development platform |
| Linux aarch64 | 🟡 | 🟡 | Should build; not regularly tested |
| macOS x86_64 / arm64 | ✅ | ✅ | Build code path under `src/common/network/macos` |
| Windows x86_64 (MSVC 2019+) | ✅ | 🟡 | Build code path under `src/common/network/windows`; less smoke time than Linux |
| FreeBSD / OpenBSD | ❌ | ❌ | Not attempted |
| 32-bit targets | 🚫 | 🚫 | Out of scope |

---

## Compilers and toolchains

| Toolchain | Status | Notes |
|---|:---:|---|
| GCC 9+ | ✅ | C++17 |
| Clang 10+ | ✅ | Including ASan / UBSan / TSan / libFuzzer |
| MSVC 2019+ | 🟡 | Builds; CI coverage missing |
| Apple Clang | ✅ | |

---

## Build systems

| System | Status | Notes |
|---|:---:|---|
| CMake ≥ 3.16 | ✅ | Primary |
| Bazel | 🟡 | `BUILD.bazel` files exist; less battle-tested than CMake |
| Makefile | 🚫 | Not provided |

---

## Sanitizers and dynamic analysis

| Tool | Status | Notes |
|---|:---:|---|
| AddressSanitizer | ✅ | Clean on `quicx_utest` |
| UndefinedBehaviorSanitizer | ✅ | Clean on `quicx_utest` |
| ThreadSanitizer | ✅ | Clean on `quicx_utest` |
| MemorySanitizer | 🟡 | Requires instrumented BoringSSL; not part of routine validation |
| libFuzzer (frame / packet / qpack / varint) | ✅ | `-DENABLE_FUZZING=ON`, smoke clean |
| Valgrind | 🟡 | Works for short runs; not part of CI |

---

## Interoperability (simulator)

The interop matrix is regenerated per release. The current detailed report is
[`reports/interop_status.md`](../reports/interop_status.md).

Summary for **v0.1.0**:

- **`handshake`** scenario: passes against the majority of mainstream peers
  (quinn, msquic, ngtcp2, neqo, lsquic, picoquic, quic-go, mvfst, aioquic).
- **`transfer`** scenario: passes against most peers; a few have known issues
  documented in `quic_interop_sim_issues.md`.
- Advanced scenarios (`multiconnect`, `resumption`, `keyupdate`, `chacha20`,
  `retry`, `zerortt`, `http3`) are partially covered; see the interop status
  document for the per-pair grid.

---

## Known limitations summary (read this before adopting)

1. **No Multipath / DATAGRAM / ACK Frequency** — applications needing these
   should not adopt v0.1.x.
2. **Cross-platform CI is missing** — Windows and macOS are developer-tested
   but not continuously verified.
3. **Public API may change** in any `0.x` minor release.
4. **No SLA on security response time** beyond the best-effort targets in
   [`SECURITY.md`](../../../SECURITY.md).
5. **mTLS, Trailers, connection pooling** have working code but limited
   end-to-end validation.

---

## Roadmap pointers

- v0.2.0 — Linux/macOS/Windows CI; DATAGRAM frames (planned)
- v0.3.0 — Multipath QUIC investigation; ACK Frequency
- v1.0.0 — API freeze, SemVer guarantees take effect

See [`maturity_roadmap.md`](../../internal/maturity_roadmap.md) for the longer-term view.
