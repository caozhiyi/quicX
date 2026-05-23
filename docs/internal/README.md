# Internal Documents

> **Audience: QuicX maintainers and contributors. Not part of the user-facing
> documentation set.**

Files in this directory are kept in-tree for traceability — release plans,
sequential code-review reports, performance root-cause analyses, refactor
proposals, and per-peer interop debugging notes. They reflect *the state of
work in progress at the time of writing* and are intentionally **not**
referenced from [`../../README.md`](../../README.md),
[`../../README_cn.md`](../../README_cn.md), or the user-facing trees under
[`../en/`](../en) / [`../zh/`](../zh).

If you are integrating QuicX into your project, you can ignore this directory.
The supported, versioned contract lives in:

- [`../en/reference/`](../en/reference) / [`../zh/reference/`](../zh/reference) — support matrix, API stability, qlog event coverage
- [`../en/reports/`](../en/reports) / [`../zh/reports/`](../zh/reports) — point-in-time interop and performance snapshots
- [`../../CHANGELOG.md`](../../CHANGELOG.md) — release notes
- [`../../SECURITY.md`](../../SECURITY.md) — security policy

## Index

| File | Purpose |
| :--- | :--- |
| [`maturity_roadmap.md`](maturity_roadmap.md) | Long-term roadmap toward 1.0 |
| [`improvement_plan.md`](improvement_plan.md) | Cross-cutting code-quality improvement plan |
| [`code_review_report.md`](code_review_report.md) / [`V2`](code_review_report_v2.md) / [`V3`](code_review_report_v3.md) | Sequential code-review snapshots |
| [`perf_e2e_analysis.md`](perf_e2e_analysis.md) | End-to-end performance root-cause analysis (cold-start PTO investigation) |
| [`perf_flamegraph_analysis.md`](perf_flamegraph_analysis.md) | Sampling-profile root-cause analysis & fix log |
| [`quic_interop_sim_issues.md`](quic_interop_sim_issues.md) | Per-peer interop debugging log (ns-3 sim mode) |
| [`pool_alloter_frame_optimization.md`](pool_alloter_frame_optimization.md) | PoolAlloter / frame allocation feasibility study (deferred) |

## Lifecycle policy

- New internal documents land here directly.
- When a document graduates into a stable contract (e.g. a feasibility study
  becomes an actual API change), promote it to `docs/{en,zh}/reference/`.
- When a document graduates into a how-to (e.g. a debugging log becomes a
  reusable runbook), promote it to `docs/{en,zh}/guide/`.
- One-off "did this run pass" snapshots that are safe to publish belong in
  `docs/{en,zh}/reports/`, not here.
- Once `v0.1.0` ships, [`../release_plan_v0.1.0.md`](../release_plan_v0.1.0.md)
  is archived into this directory and a fresh `RELEASE_PLAN_v0.x.y.md` is
  created at the `docs/` top level for the next iteration.
