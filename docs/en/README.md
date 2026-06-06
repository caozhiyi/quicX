# QuicX Documentation (English)

This directory is the **documentation map** of QuicX — categorizing all documents under `docs/en/` by responsibility and pointing out what questions each document answers. It does not teach you how to use QuicX itself; for the entry points, please refer to the repository root README and Sections 1 and 2.

---

## 1. Getting Started (`getting-started/`)

Minimal path: First build QuicX, then run your first HTTP/3 hello world.

| Document | What question does it answer? |
| :--- | :--- |
| [`getting-started/build.md`](getting-started/build.md) | How to integrate QuicX with CMake / Bazel, using `add_subdirectory` and `find_package` |
| [`getting-started/quick_start.md`](getting-started/quick_start.md) | How to run the first HTTP/3 hello world, and what behavior to expect |

## 2. Tutorials (`tutorial/`)

Three API walk-throughs needed when writing your first non-demo program.

| Document | What question does it answer? |
| :--- | :--- |
| [`tutorial/http3_api_guide.md`](tutorial/http3_api_guide.md) | HTTP/3 application layer API: routing, middleware, Server Push, streaming body |
| [`tutorial/quic_api_guide.md`](tutorial/quic_api_guide.md) | QUIC transport layer API: raw streams, custom RPC tunnels |
| [`tutorial/configuration_reference.md`](tutorial/configuration_reference.md) | Meanings and default values of various parameters in `QuicConfig` / `Http3Config` |

## 3. Guides (`guide/`)

How-to style material — operational guides that are neither contracts nor tutorials, consult as needed.

| Document | What question does it answer? |
| :--- | :--- |
| [`guide/perf_testing.md`](guide/perf_testing.md) | Use of performance testing and profiling tools |
| [`guide/ci_local.md`](guide/ci_local.md) | Reproducing the isomorphic environment of GitHub Actions CI locally |
| [`guide/interop_overview.md`](guide/interop_overview.md) | How the `quic-interop-runner` interoperability testing framework works |
| [`guide/interop_runbook.md`](guide/interop_runbook.md) | Interop testing runbook: commands and scenarios |
| [`guide/sanitizer_hello_world_load.md`](guide/sanitizer_hello_world_load.md) | Sanitizer scenario: hello_world load generation |
| [`guide/sanitizer_file_transfer.md`](guide/sanitizer_file_transfer.md) | Sanitizer scenario: file_transfer |

> Note: `perf_testing.md` and `ci_local.md` are currently only available in Chinese; the English versions are placeholder documents pending translation.

## 4. Reference (`reference/`)

Authoritative documents that downstream projects can rely on, updated infrequently.

| Document | What question does it answer? |
| :--- | :--- |
| [`reference/support_matrix.md`](reference/support_matrix.md) | Platform, toolchain, and Sanitizer support matrix |
| [`reference/api_stability.md`](reference/api_stability.md) | Public headers inventory and API stability policy |
| [`reference/qlog_event_coverage.md`](reference/qlog_event_coverage.md) | QLog event coverage list (implemented / not covered) |

Release notes and security policy are at the repo root:
[`../../CHANGELOG.md`](../../CHANGELOG.md) ·
[`../../SECURITY.md`](../../SECURITY.md) ·
[`../../CONTRIBUTING.md`](../../CONTRIBUTING.md).

## 5. Reports (`reports/`)

Time-stamped result snapshots, replaced by new versions after each round of testing — **non-contractual**.

| Document | Scope |
| :--- | :--- |
| [`reports/interop_status.md`](reports/interop_status.md) | Latest interoperability test results with external QUIC implementations |
| [`reports/performance_baseline.md`](reports/performance_baseline.md) | Performance baseline (CPU hotspots, Buffer / Frame / Packet throughput) |

> Note: `performance_baseline.md` is currently only available in Chinese; the English version is a placeholder document pending translation.

## 6. Design Notes (`design/`)

Internal conventions worth knowing when integrating or extending QuicX. This section is not an RFC discussing "what to do in the future", but describes the **existing invariants in the current code**.

The documentation is extensive and divided into five groups by responsibility.

### 6.1 Main Path

Understand how a datagram / connection / handshake is processed.

| Document | What question does it answer? |
| :--- | :--- |
| [`design/packet_lifecycle.md`](design/packet_lifecycle.md) | The complete path of a datagram from socket input to the upper-layer frame |
| [`design/connection_anatomy.md`](design/connection_anatomy.md) | Three-layer structure of the Connection subtree (21 cpp files): Skeleton / Coordinator / Controller |
| [`design/handshake_state_machine.md`](design/handshake_state_machine.md) | State machine for TLS / Encryption Levels / Key Update |
| [`design/ownership_and_memory.md`](design/ownership_and_memory.md) | Ownership and lifecycle of Buffer / Connection / Stream |

### 6.2 Key Decisions

Algorithms, protocols, and optimization trade-offs involving "why we did this".

| Document | What question does it answer? |
| :--- | :--- |
| [`design/loss_recovery.md`](design/loss_recovery.md) | PTO / loss timer / ACK handling (RFC 9002) |
| [`design/congestion_control.md`](design/congestion_control.md) | Reno / Cubic / BBR v1/v2/v3 implementation trade-offs and pluggable mechanism |
| [`design/qpack_dynamic_table.md`](design/qpack_dynamic_table.md) | Collaboration of QPACK dynamic table with encoder/decoder streams |

### 6.3 Infrastructure

Underlying mechanisms supporting the main path, consult as needed.

| Document | What question does it answer? |
| :--- | :--- |
| [`design/process_model.md`](design/process_model.md) | master + worker process model, cross-thread channels, and why we don't use thread pools |
| [`design/timer_design.md`](design/timer_design.md) | Trade-off between timing wheel vs treemap two-layer timers |
| [`design/pool_alloter.md`](design/pool_alloter.md) | Why frame-level memory pools are needed and how they complement existing optimizations |
| [`design/udp_io.md`](design/udp_io.md) | GSO / sendmmsg / recvmmsg trade-offs and fallback paths |

### 6.4 Protocol Details

Protocol details walkthrough by RFC chapters.

| Document | What question does it answer? |
| :--- | :--- |
| [`design/stream_state_machine.md`](design/stream_state_machine.md) | Stream rx/tx dual state machines (RFC 9000 §3) |
| [`design/crypto_keying.md`](design/crypto_keying.md) | TLS key derivation and Key Update (RFC 9001 §5/§6) |
| [`design/h3_connection.md`](design/h3_connection.md) | H3 control stream / QPACK encoder/decoder stream multi-stream collaboration |
| [`design/upgrade_negotiation.md`](design/upgrade_negotiation.md) | H1 -> H3 negotiation, Alt-Svc, and Upgrade protocol headers interaction |

### 6.5 Observability

| Document | What question does it answer? |
| :--- | :--- |
| [`design/metrics.md`](design/metrics.md) | Built-in Metrics catalog and emission points |

## 7. Further Reading of Source Code

Beyond documentation, the source code itself is the best reference:

- **`example/`** and **`test/`** are "executable documentation" — check `example/hello_world` for usage, `test/quic/` for protocol module unit tests, and `tools/cc_simulator/` for congestion control visualization.
- The repository directory structure itself serves as an index: each subdirectory under `src/quic/` roughly corresponds to chapters of RFC 9000 / 9001 / 9002; simply `cd` into them as needed.
- The source code at key decision points (congestion control / loss recovery / flow control / handshake / QPACK) contains nearby RFC section-level comments; if you have doubts, directly check the clauses referenced in the comments.
