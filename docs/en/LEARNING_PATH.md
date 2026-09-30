# QuicX Source-Code Learning Path

QuicX is a **production-oriented** QUIC / HTTP/3 protocol stack: 1523 unit and integration tests, clean ASan / UBSan / TSan runs, and a 90.60% pass rate across the interop matrix; the code is at the same time deliberately readable, well-commented, with self-consistent tests and documentation. Since 1.0, the public C++ API follows semantic versioning — see [`api_stability.md`](./reference/api_stability.md).

This document is the **main entry point** for reading the source: ordered by "the complete journey of a packet from the NIC to an HTTP/3 handler", each station lists its source directory, the accompanying design documents, and hands-on experiments. All paths are relative to the repo root.

---

## Preparation Before Reading

1. Read through the architecture diagram and feature table in `README.md`, to build a global impression.
2. Prepare the build environment and get the minimal example `example/hello_world/` running:
   ```bash
   mkdir build && cd build && cmake .. && make -j
   # then follow the README / comments inside example/hello_world/ to start the client and server
   ```
3. Run the test suite once to confirm a healthy environment: `python3 run_tests.py` (the unit-test binary lives at `build/bin/quicx_utest`).

**General principle**: at every station, read the design document first to build a mental model, then cross-reference the source, and finally verify your understanding with the corresponding unit tests (`test/unit_test/`).

---

## Station 0: Examples & Public API — `example/` and `include/quicx/`

- **Goal**: know what QuicX looks like from the **user's perspective**.
- **Source entry**: `example/hello_world/` (the minimal runnable client/server).
- **Full API**: the four public header directories under `include/quicx/` — `common/`, `quic/`, `http3/`, `upgrade/` (authoritative list in [`api_stability.md`](./reference/api_stability.md)).
- **Hands-on**: modify hello_world — add a custom HTTP route / print a stream's data.

## Station 1: Threading & Event Model — `src/quic/quicx/`, `src/common/thread/`

- **Goal**: understand how workers / event loops are organized (QuicX's skeleton).
- **Source entry**: `src/quic/quicx/` (workers and scheduling), `src/common/thread/`.
- **Companion doc**: [`design/process_model.md`](./design/process_model.md).

## Station 2: UDP I/O — `src/quic/udp/`

- **Goal**: how packets actually enter and leave the NIC; how send/receive buffers connect.
- **Source entry**: `src/quic/udp/` (reading, writing and dispatch of UDP sockets).
- **Companion doc**: [`design/udp_io.md`](./design/udp_io.md).
- **Hands-on**: use the udp-related cases in `test/unit_test/` to observe packet splitting and coalescing.

## Station 3: Packet Parsing — `src/quic/packet/`

- **Goal**: QUIC long/short headers, version negotiation, and the encode/decode of every packet type — Initial / 0-RTT / Handshake / 1-RTT.
- **Source entry**: `src/quic/packet/`; start from the `DecodePackets` entry in `packet_decode.cpp` and follow the type dispatch downward.
- **Companion doc**: [`design/packet_lifecycle.md`](./design/packet_lifecycle.md).
- **Experiment**: `test/perf/packet_perf_test.cpp` provides encode/decode benchmarks for every packet type; `docs/internal/perf_flamegraph_analysis.md` records the full investigation of a real "195× performance mystery on the dispatch path" — highly recommended reading.

## Station 4: Crypto & Handshake — `src/quic/crypto/`

- **Goal**: how TLS 1.3 (BoringSSL) is embedded into QUIC: Initial key derivation, 0-RTT / 1-RTT key scheduling, the handshake state machine.
- **Source entry**: `src/quic/crypto/`.
- **Companion docs**: [`design/crypto_keying.md`](./design/crypto_keying.md),
  [`design/handshake_state_machine.md`](./design/handshake_state_machine.md).

## Station 5: Frame Encode/Decode — `src/quic/frame/`

- **Goal**: the structure and encode/decode of STREAM / ACK / CRYPTO / FLOW_CONTROL and other frames, plus the mapping from frames to stream / connection state.
- **Source entry**: `src/quic/frame/` (one class per frame kind; read each against RFC 9000).

## Station 6: Streams & Flow Control — `src/quic/stream/`

- **Goal**: bidirectional / unidirectional stream state machines, send and receive buffers, stream-level and connection-level flow control.
- **Source entry**: `src/quic/stream/`.
- **Companion doc**: [`design/stream_state_machine.md`](./design/stream_state_machine.md).

## Station 7: Connection & Loss Recovery — `src/quic/connection/`

- **Goal**: how the connection object strings together all the modules above: packet construction, ACK processing, PTO / loss detection and retransmission.
- **Source entry**: `src/quic/connection/`.
- **Companion docs**: [`design/connection_anatomy.md`](./design/connection_anatomy.md),
  [`design/loss_recovery.md`](./design/loss_recovery.md).

## Station 8: Congestion Control — `src/quic/congestion_control/`

- **Goal**: Reno-family congestion-window evolution, pacing, and the interplay with loss recovery.
- **Source entry**: `src/quic/congestion_control/`.
- **Companion doc**: [`design/congestion_control.md`](./design/congestion_control.md).
- **Experiment**: `test/congestion_control/` and `test/perf/congestion_control_perf_test.cpp`.

## Station 9: HTTP/3 — `src/http3/`

- **Goal**: the control and request streams, the frame family beyond QPACK (HEADERS / DATA / SETTINGS / GOAWAY…), routing and server push.
- **Source entry**: `src/http3/frame/` (frames), `src/http3/stream/` (streams), `src/http3/router/` (routing), `src/http3/http/` (HTTP semantics).
- **Companion doc**: [`design/h3_connection.md`](./design/h3_connection.md).

## Station 10: QPACK — `src/http3/qpack/`

- **Goal**: static / dynamic tables of header compression, the encoder / decoder instruction streams.
- **Source entry**: `src/http3/qpack/`.
- **Companion doc**: [`design/qpack_dynamic_table.md`](./design/qpack_dynamic_table.md).

---

## Cross-Cutting Concerns (intersperse reading anytime)

| Topic | Source | Design Doc |
|---|---|---|
| Buffer abstraction | `src/common/buffer/` | — |
| Timers (TreeMapTimer) | `src/common/timer/` | [`design/timer_design.md`](./design/timer_design.md) |
| Memory pools / slab allocation | `src/common/allocator/`, `src/common/structure/` | [`design/pool_allocator.md`](./design/pool_allocator.md), [`design/ownership_and_memory.md`](./design/ownership_and_memory.md) |
| Logging | `src/common/log/` | — |
| Metrics (the /metrics Prometheus endpoint) | `src/common/metrics/`, `src/http3/metric/` | [`design/metrics.md`](./design/metrics.md) |
| qlog | `src/common/qlog/` | — |
| 0-RTT / Retry / upgrade negotiation | `src/quic/`, `src/http3/` | [`design/upgrade_negotiation.md`](./design/upgrade_negotiation.md) |

## Verification and Beyond

1. **Unit tests**: `test/unit_test/` (~1500+ cases); pick by module and read the behavioral contracts.
2. **Benchmarks**: `test/benchmarks/` and `test/perf/`; for flame-graph tooling see `scripts/perf/generate_flamegraph.sh` and `docs/internal/perf_flamegraph_analysis.md`.
3. **Interoperability**: QuicX's 24-scenario interop results against 17 mainstream QUIC implementations, plus the runbook: [`reports/interop_status.md`](./reports/interop_status.md), [`guide/interop_runbook.md`](./guide/interop_runbook.md).
4. **Ops dashboard**: the `/metrics` endpoint + `tools/grafana/quicx_dashboard.json`.

> Found a documentation/code mismatch, or want to improve a station's explanation? Feel free to file an issue / PR per `CONTRIBUTING.md`.
