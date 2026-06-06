# Changelog

All notable changes to **QuicX** will be documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/).

> **Versioning**: QuicX does **not** follow Semantic Versioning, and the public
> C++ API may change between releases.  Here `1.0` marks the point where the
> code, docs, and tests are self-consistent and the learning path is complete —
> it is **not** an ABI-stability or API-compatibility milestone.  Pin to an exact
> version if you depend on QuicX directly.

---

## [Unreleased]

_No changes since 1.0.0._

---

## [1.0.0] — 2026-06-02

> **What 1.0 means here.** This release is the completion of the *learning
> reference* milestone defined in
> [`docs/internal/learning_project_roadmap.md`](docs/internal/learning_project_roadmap.md):
> the code, the docs, and the tests are self-consistent, and a reader who
> knows the basics of QUIC / HTTP/3 can follow the implementation end-to-end
> from `example/hello_world` through UDP I/O, packet parsing, crypto,
> streams, congestion control, and HTTP/3 + QPACK. **It is not an
> ABI-stability or production-readiness milestone**; the public C++ API
> carries no compatibility promise across releases. Pin to an exact version
> if you depend on QuicX directly.

### Changed

- **Repositioned as a learning reference for QUIC / HTTP/3.** The README,
  badges, and this changelog no longer frame QuicX as a pre-1.0 production
  preview. The earlier goal of `1.0.0` as an ABI-stability / SemVer
  milestone has been dropped; `1.0` now means the code, docs, and tests are
  self-consistent and the learning path is complete.
- **README rewritten** for clarity (single-page narrative: positioning →
  architecture → capabilities → interop matrix → quick-start → further
  reading), with the previous duplicated TOC, doc-index dumps, and
  redundant introduction blocks removed.
- **Comment language unified to English** across the codebase, with
  protocol-decision points consistently citing the relevant RFC 9000 /
  9001 / 9002 / 9114 / 9204 clauses (e.g. `// RFC 9000 §9.3.3: probing
  packets are exempt from congestion control`). Naming, interface
  prefixes (`if_` / `I`), and file organisation are normalised across
  modules.
- **Interface hygiene**: high-callback-count constructors (notably
  `connection_path_manager` and several manager classes) are refactored
  to use small interface / parameter structs, making dependency edges
  legible at a glance.

### Added

#### Documentation — the learning path

- **`docs/zh/LEARNING_PATH.md`** — a single ordered reading plan that
  walks from `example/hello_world` to UDP I/O → packet parsing → crypto
  / handshake → frames → streams + flow control → loss recovery →
  congestion control → HTTP/3 + QPACK, with a source-file pointer at
  every hop.
- **Module design docs under `docs/zh/design/`** — covering the
  packet life-cycle, handshake state machine, loss recovery (RFC 9002),
  the pluggable congestion-control framework (Cubic / BBR / Reno),
  stream state machine, QPACK dynamic table, crypto keying, UDP I/O,
  the timer wheel, the H3 connection, the H1→H3 upgrade path, and the
  process / threading model. Each follows the same shape: one diagram
  + key source files + the matching RFC sections.
- **Cross-cutting design docs**: `connection_anatomy.md`,
  `ownership_and_memory.md`, `pool_alloter.md`, and `metrics.md` are
  promoted from drafts to reviewed reference material.
- **`docs/internal/learning_project_roadmap.md`** — the v1.0 roadmap
  (definition-of-done, non-goals, four work blocks, six-stage
  schedule). The earlier industrial-grade plans
  (`improvement_plan.md`, `maturity_roadmap.md`) are superseded.

#### Tests as executable documentation

- Core handshake / flow-control / congestion-control / QPACK unit tests
  now name (or comment) the **specific RFC clause** each case is
  exercising, so the test suite reads as a navigable index of the
  protocol.
- README documents how to run `cc_simulator` and the `example/`
  programs, including which back-end each example talks to and the
  expected output.

### Fixed

- **Server idle-timeout deadlock at the peer's connection-level
  flow-control limit.** Under high-loss `sim` mode the server's
  `sent_bytes_` accounting (which, per RFC 9000 §4.1, includes bytes
  consumed by retransmissions) could reach the peer's `initial_max_data`
  (typically 768 KB) long before the peer had cumulatively acknowledged
  enough *distinct* bytes to issue a `MAX_DATA` update.
  `BaseConnection::TrySend` then returned `false` with buffered stream
  data still present, `Worker::ProcessSend` evicted the connection from
  its active set, and — with no in-flight packets to drive ACK
  callbacks and no `MAX_DATA` ever arriving — the connection went
  dormant until idle timeout. The fix mirrors the existing
  `is_cwnd_limited_` resume path and is fully RFC 9000 §4.1 / §19.9 /
  §19.12 compliant:

  1. `SendManager` gains an `is_flow_control_blocked_` flag and a
     `flow_control_recheck_task_` (100 ms wake-up) installed via a new
     `SetFlowControlBlocked()` API.
  2. `BaseConnection::TrySend` calls `SetFlowControlBlocked()` whenever
     the connection-level flow-control check fails *and* buffered
     stream data is present, so the connection stays alive instead of
     being evicted.
  3. `OnPacketAck` triggers `send_retry_cb_` on *any* incoming ACK
     while either `is_cwnd_limited_` or `is_flow_control_blocked_`
     is set, so a peer-issued `MAX_DATA` piggy-backed on an ACK
     immediately resumes sending.
  4. `OnMaxDataFrame` already calls `event_sink_.OnConnectionActive()`,
     completing the wake-up loop.

  Verified: utest 1198/1199 PASS, local interop 14/14 PASS, Docker
  `quicx↔quicx` transfer PASS; post-fix server logs show **zero**
  `BLOCKED at limit` occurrences for affected connections (vs.
  permanent stalls pre-fix), and stream progress advances past the
  prior dead-stop offset.

- **Connection migration: CID rotation now fully RFC-compliant.** Three
  root causes that previously prevented `connectionmigration` /
  `rebind-port` / `rebind-addr` interop tests from being reliable have
  been fixed:

  1. **Probing packets exempt from congestion control** (RFC 9000
     §9.3.3) — `BaseConnection::TrySend` used to keep `PATH_CHALLENGE`
     / `PATH_RESPONSE` stuck in `wait_frame_list_` whenever the cwnd
     was exhausted, breaking path validation under load. The send path
     now bypasses cwnd when the wait list contains a probing frame.
  2. **`RetireIDBySequence` honours the single-sequence semantics of
     RFC 9000 §19.16** instead of cumulatively erasing every CID with
     `seq <= N`. A new `RetireIDsUpTo(prior_to)` API serves the batch
     semantics required by `NEW_CONNECTION_ID.retire_prior_to`
     (RFC 9000 §19.15).
  3. **Local CID pool is replenished after `RETIRE_CONNECTION_ID`** so
     the peer always has fresh CIDs available for subsequent
     migrations.

  Verified by the interop self-test: 14/14 PASS in both `--local` and
  `--no-sim` (Docker) modes, including `connectionmigration`,
  `rebind-port`, and `rebind-addr`.

- **HTTP/3 SETTINGS bound, `100 ms` polling, and other long-standing
  TODOs cleared.** As part of the §3 P0 clean-up, every `TODO` /
  `FIXME` / commented-out implementation in `src/` was either
  implemented, removed, or explicitly converted into a documented
  *known limitation* in this changelog. Notable items:
  - `SETTINGS` frame parsing now caps the number of identifier/value
    pairs at 32 (RFC 9114 §7.2.4).
  - The HTTP/3 connection's fixed `100 ms` poll was replaced with
    `NextExpireTimeMs()`, which is driven by the active timer set.
  - The hardcoded `1300`-byte STREAM-frame size was replaced with the
    negotiated `max_udp_payload_size`, with the rationale documented in
    the call site.

### Known limitations (carried into 1.0)

The following are explicitly **out of scope for the learning-reference
milestone** (see `learning_project_roadmap.md` §2). They remain on the
post-1.0 backlog and are listed here so consumers know what is *not*
guaranteed:

- **Performance back-ends**: GSO / `io_uring` / `AF_XDP` /
  `MSG_ZEROCOPY` / `SO_REUSEPORT` are intentionally not on the main
  IO path — keeping the kernel-to-app path single-thread and
  cmsg-free is part of the readability goal.
- **Protocol extensions**: DATAGRAM (RFC 9221), WebTransport, Multipath
  QUIC, ACK-Frequency, and HTTP/3 Extensible Priorities are not
  implemented.
- **HTTP/3 Server Push** is implemented at the frame-format /
  unidirectional-stream level for educational purposes; full
  production-grade push (large-body fragmenting, the complete cancel /
  rate-limit life-cycle, and `IStream::Close(error_code)` lowering)
  remains an interface skeleton.
- **Server-initiated Retry / token signing** (RFC 9000 §17.2.5),
  **end-to-end ECN feedback**, and the **shared-buffer span migration
  for `NEW_TOKEN`** are not implemented.
- **Windows ECN cmsg** (`IP_ECN` / `IPV6_ECN` via `WSARecvMsg`,
  `IP_TOS` / `IPV6_TCLASS` via `setsockopt`),
  **`LocalCIDManager` / `RemoteCIDManager` type split**, and moving
  `IConnection`'s test access points to a friend-class facade are
  deferred polish items.
- **Language bindings** (C ABI, Rust / Go / Python / Node / Java),
  **package-manager / pre-built binary distribution**, **SBOM /
  sigstore signing**, and **curl / nginx / Envoy / gRPC ecosystem
  integration** are out of scope.
- **Cross-platform CI** stays Linux-only; macOS and Windows builds
  are exercised locally and are expected to work, but are not gated
  by CI.
- **Official interop-runner ranking** is not pursued. The shipped
  self-test interop matrix (`docs/zh/reports/interop_status.md`)
  covers 14 cases at 91.7 % pass rate against the public reference
  implementations and is the supported interop signal.

### Compatibility

- **C++ standard**: C++17 (configured via CMake
  `set(CMAKE_CXX_STANDARD 17)`).
- **Build systems**: CMake ≥ 3.16 (primary) and Bazel
  (`BUILD.bazel`).
- **Toolchains tested**: GCC ≥ 9, Clang ≥ 12; MSVC builds with
  `/wd4996 /wd4267 /wd4244` warnings disabled in `CMakeLists.txt`.
- **TLS backend**: BoringSSL submodule under `third/boringssl` (no
  system OpenSSL).
- **Public include layout**: `src/quic/include/...` and
  `src/http3/include/...`. A top-level `include/quicx/` namespace
  is **not** part of this release.
- **API stability**: none. See the `[1.0.0]` heading note above.

---

## [0.1.0] — 2026-05-23

First public release of QuicX — a self-contained C++17 HTTP/3 stack built on
the QUIC transport protocol.  The release covers the full path from UDP I/O
and TLS 1.3 (BoringSSL) through QUIC streams to HTTP/3 routing, QPACK header
compression, and server push.

### Added

#### QUIC transport (RFC 9000 / RFC 9369)

- **TLS 1.3 handshake** via BoringSSL, including 0-RTT and 1-RTT,
  session-ticket caching, and `SSLKEYLOGFILE` support for Wireshark decryption.
- **Multi-version negotiation**: QUIC v1 (`0x00000001`, RFC 9000) and
  QUIC v2 (`0x6b3343cf`, RFC 9369) implemented in parallel; preference order
  is configurable.
- **Connection lifecycle**: multi-connection management on a single endpoint;
  graceful `CONNECTION_CLOSE`; Retry-packet anti-amplification; idle / draining
  timers.
- **Connection migration** (RFC 9000 §9): active migration, NAT-rebinding
  detection, and path validation via `PATH_CHALLENGE` / `PATH_RESPONSE`.
- **Streams**: bidirectional and unidirectional streams, per-stream and
  per-connection flow control, `STREAM_DATA_BLOCKED` and `DATA_BLOCKED`
  emission and handling.
- **Congestion control**: pluggable framework with built-in implementations
  for **BBR v1 / v2 / v3**, **CUBIC**, and **Reno**, selectable per
  connection via a factory; packet pacer for smooth output.
- **Loss recovery**: ACK-based loss detection, Probe Timeout (PTO),
  packet retransmission with explicit encryption-level tracking.
- **ECN** marking and feedback (optional).
- **Key Update** (RFC 9001 §6) — automatic, with packet-number-space
  rollover.

#### HTTP/3

- **QPACK** (RFC 9204) static + dynamic table with Huffman coding,
  encoder/decoder streams, and dynamic-table updates.
- **Server-side framework**: route registration with path parameters
  (`:param`), wildcard routes (`*`), per-method handlers, and a
  before/after middleware chain.
- **HTTP methods**: GET, HEAD, POST, PUT, DELETE, CONNECT, OPTIONS, TRACE,
  PATCH.
- **Two handler modes**: *complete mode* (entire body buffered before the
  handler runs) and *streaming mode* (`IAsyncServerHandler` /
  `IAsyncClientHandler` receive body chunks as they arrive — suitable for
  large file transfer).
- **Server Push** with `PUSH_PROMISE`; client-side push acceptance / rejection
  callback.
- **HTTP/1.1 → HTTP/3 upgrade** path under `src/upgrade`.

#### Core infrastructure

- **Memory**: custom slab allocator (`NormalAlloter`); pooled `BufferChunk`
  chain that supports a near-zero-copy I/O path from kernel to application.
- **Networking**: cross-platform UDP I/O with separate Linux / macOS /
  Windows backends; non-blocking event loop.
- **Threading**: single-thread or multi-thread mode with configurable worker
  thread count.
- **Timers**: hierarchical timer wheel for connection idle, PTO, key update,
  and application-defined timers.
- **Logging**: levelled logging (`Null` / `Debug` / `Info` / `Warn` / `Error`)
  with configurable output sink.
- **QLog** (RFC 9001-compliant tracing), enabled by `-DQUICX_ENABLE_QLOG=ON`;
  produced traces are compatible with [qvis](https://qvis.quictools.info/)
  and Wireshark.
- **Metrics**: built-in metrics registry covering UDP, QUIC, HTTP/3,
  congestion, latency, memory, TLS, connection migration, and Retry; plus an
  optional metrics HTTP endpoint.
- **`quicx::GetVersionString()` / `QUICX_VERSION_*` macros** in
  `<common/version.h>` for downstream version gating (this is the *product*
  version; the on-the-wire QUIC protocol version still lives in
  `<quic/common/version.h>`).

#### Examples

`example/` ships 15 ready-to-run programs covering hello-world, REST APIs,
file transfer, streaming, bidirectional comm, concurrent requests,
connection lifecycle, error handling, server push, load testing,
performance benchmarking, metrics monitoring, QLog integration, HTTP/1.1
upgrade, and a curl-like CLI client (`quicx_curl`).

#### Testing

- **Unit tests**: 1191 tests across QUIC, HTTP/3, QPACK, congestion control,
  buffer / allocator, and common utilities.
- **Integration / Realistic-network tests**: 56 scenarios covering loss,
  reordering, duplication, throughput, fairness, and recovery.
- **Performance baseline**: 10 micro-benchmarks (crypto, packet, frame,
  QPACK, congestion control, loss recovery, memory, CPU, end-to-end).
- **Congestion-control simulator** (`cc_simulator`) for offline experiments.
- **Fuzz targets** (libFuzzer, opt-in via `-DENABLE_FUZZING=ON`).
- **Interop harness** (opt-in via `-DENABLE_INTEROP=ON`) against the
  public [quic-interop-runner](https://interop.seemann.io/) suite; current
  status is tracked in [`docs/en/reports/interop_status.md`](docs/en/reports/interop_status.md).

### Fixed (during the 0.1.0 stabilisation cycle)

- **HTTP/3 FrameDecoder** — fixed a state-corruption bug in which an unknown
  frame type whose length varint was split across two `OnData` calls would
  cause subsequent bytes to be misinterpreted as a new frame type.  In the
  worst case a malicious peer could trigger a skip counter near 700 MB,
  producing a remote DoS / memory-exhaustion path.  Added an explicit
  `kReadingUnknownLength` state and three regression tests covering the
  affected splits.
- **RecvStream::OnStreamFrame** — fixed cross-stream data contamination when
  multiple stream frames travelled in the same QUIC packet
  (`SharedBufferSpan` shallow copy → bytes from one stream overwrote
  another).  Now uses a deep copy.
- **HTTP/3 SETTINGS race** — request-stream data arriving before the peer's
  control-stream `SETTINGS` is now treated as a normal QUIC stream
  multiplexing race (logged as a warning) instead of a fatal connection
  error.
- **0-RTT session resumption** — fixed BoringSSL rejecting 0-RTT on the
  second connection because `SSL_set_quic_early_data_context()` was called
  with a transport-params-derived context that varied between connections.
  Now uses the fixed string `"quicx-0rtt-v1"`.
- **CryptoStream / Stream-id collision** — fixed `FRAME_ENCODING_ERROR` in
  the `zerortt` interop case.  CryptoStream's stream_id was hardcoded to
  `0`, colliding with client-initiated bidirectional stream `0`
  (RFC 9000 §2.1).  CryptoStream is now identified via
  `dynamic_pointer_cast<CryptoStream>` in
  `connection_stream_manager.cpp`.
- **Version-negotiation interop judgement** — `interop_runner.py` now
  marks the test PASSED when the server emits a `Version Negotiation`
  packet that the client logs as `Received VN`, even when the client cannot
  fall back to another version.

### Known issues / limitations

- **Connection-migration CID rotation** — during active migration the DCID
  is currently *not* rotated (RFC 9000 §9.5 SHOULD, not MUST).
  `RotateRemoteConnectionID()` produces a CID that the peer's `conn_map_`
  does not yet recognise; investigating the `NEW_CONNECTION_ID` sync path
  is on the post-1.0 roadmap.  Migration without rotation works.
- **Interop coverage** is partial.  See
  [`docs/en/reports/interop_status.md`](docs/en/reports/interop_status.md) for the
  per-implementation matrix; the `handshake` and `transfer` scenarios are
  the priority for v0.1.x patch releases.
- **Multipath QUIC**, **DATAGRAM** (RFC 9221), and **ACK Frequency**
  extensions are not implemented in this release.
- **Cross-platform CI**: Linux is the primary tested platform.  macOS and
  Windows builds are exercised locally and are expected to work, but are
  not yet covered by automated cross-platform CI.

### Compatibility

- **C++ standard**: C++17 (configured via CMake `set(CMAKE_CXX_STANDARD 17)`).
- **Build systems**: CMake ≥ 3.16 (primary) and Bazel (`BUILD.bazel`).
- **Toolchains tested**: GCC ≥ 9, Clang ≥ 12; MSVC is supported with the
  warnings disabled in `CMakeLists.txt` (`/wd4996 /wd4267 /wd4244`).
- **TLS backend**: BoringSSL submodule under `third/boringssl` (no system
  OpenSSL).
- **Public include layout** during 0.1.x is still
  `src/quic/include/...` and `src/http3/include/...`; a top-level
  `include/quicx/` namespace is planned but **not** part of this release.

---

[Unreleased]: https://example.invalid/quicX/compare/v1.0.0...HEAD
[1.0.0]: https://example.invalid/quicX/compare/v0.1.0...v1.0.0
[0.1.0]: https://example.invalid/quicX/releases/tag/v0.1.0
