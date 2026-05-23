# QuicX Documentation (English)

This is the English documentation tree. The 中文 mirror lives at
[`../zh/README.md`](../zh/README.md). Repository entry points:
[`../../README.md`](../../README.md) (English) ·
[`../../README_cn.md`](../../README_cn.md) (中文).

---

## 1. Getting started

| Topic | Link |
| :--- | :--- |
| Build & integrate (CMake / Bazel, `add_subdirectory` / `find_package`) | [`getting-started/build.md`](getting-started/build.md) |
| Run your first HTTP/3 hello world | [`getting-started/quick_start.md`](getting-started/quick_start.md) |

## 2. Tutorials

| Topic | Link |
| :--- | :--- |
| HTTP/3 application API (router, middleware, push, streaming body) | [`tutorial/http3_api_guide.md`](tutorial/http3_api_guide.md) |
| QUIC transport API (raw streams / custom RPC tunnels) | [`tutorial/quic_api_guide.md`](tutorial/quic_api_guide.md) |
| Configuration reference (`QuicConfig` / `Http3Config`) | [`tutorial/configuration_reference.md`](tutorial/configuration_reference.md) |

## 3. Guides (how-to)

Runbook-style material — operational guides that are neither contracts nor
tutorials.

| Topic | Link |
| :--- | :--- |
| Performance testing & profiling tools (translation pending) | [`guide/perf_testing.md`](guide/perf_testing.md) |
| Running CI locally (mirrors GitHub Actions) (translation pending) | [`guide/ci_local.md`](guide/ci_local.md) |
| Interop testing — framework overview | [`guide/interop_overview.md`](guide/interop_overview.md) |
| Interop testing — runbook (commands & scenarios) | [`guide/interop_runbook.md`](guide/interop_runbook.md) |

## 4. Reference (release contract)

Authoritative, versioned documents. Downstream projects can rely on these.

| Document | Purpose |
| :--- | :--- |
| [`reference/support_matrix.md`](reference/support_matrix.md) | Feature support matrix; platforms, toolchains, sanitizers |
| [`reference/api_stability.md`](reference/api_stability.md) | Public header inventory & API stability policy |
| [`reference/qlog_event_coverage.md`](reference/qlog_event_coverage.md) | qlog event coverage inventory (implemented / not implemented) |

Release notes and security policy are at the repo root:
[`../../CHANGELOG.md`](../../CHANGELOG.md) ·
[`../../SECURITY.md`](../../SECURITY.md) ·
[`../../CONTRIBUTING.md`](../../CONTRIBUTING.md).

## 5. Reports (point-in-time snapshots)

Time-stamped result documents — each run supersedes the previous one.
**Not part of the contract.**

| Document | Scope |
| :--- | :--- |
| [`reports/interop_status.md`](reports/interop_status.md) | Latest interoperability test results against external QUIC implementations |
| [`reports/performance_baseline.md`](reports/performance_baseline.md) | Performance baseline (CPU hotspots, buffer / frame / packet throughput) — translation pending |

## 6. Design notes

Internals worth understanding when integrating or extending QuicX. These are
not RFC-style design discussions of unimplemented work — they document
current invariants of the codebase.

| Topic | Link |
| :--- | :--- |
| Built-in metrics catalogue | [`design/metrics.md`](design/metrics.md) |
| Buffer / connection ownership model | [`design/ownership_and_memory.md`](design/ownership_and_memory.md) |

## 7. Internal (maintainers only)

Working documents, not part of the user-facing contract (roadmap, code review
reports, performance investigations, interop debugging notes). Integrators and
end users can ignore this directory. See [`../internal/README.md`](../internal/README.md).
