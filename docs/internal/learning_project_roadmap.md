# quicX 教学优先路线图（v1.0 起）


> **状态**：现行路线图（取代 [`maturity_roadmap.md`](./maturity_roadmap.md)，
> 那份文档自 v1.0.0 起转为历史快照）
> **上游依据**：`CHANGELOG.md` 的 `[1.0.0]` 与 `[Unreleased]` 段

---

## 定位

QuicX 是 **QUIC / HTTP/3 的教学参考实现**：

- 代码可读性优先，逐层贯通端到端实现（入口：
  [`../zh/LEARNING_PATH.md`](../zh/LEARNING_PATH.md)）；
- **不承诺 API 兼容性**：不遵循 SemVer，公开 C++ API 可能在版本间变更
  （详见 [`../zh/reference/api_stability.md`](../zh/reference/api_stability.md)）；
- 质量底线靠测试与互通保障，而非兼容性承诺。

## 已具备

- 24 场景 × 17 对端互通基线：综合有效通过率 **90.60%**（
  见 `docs/zh/reports/interop_status.md`）；
- `test/perf` 性能基准与 SIGPROF 采样 profiler（排查记录见
  `perf_flamegraph_analysis.md`）；
- `/metrics` Prometheus 导出端点（`src/http3/metric/metrics_handler.cpp`）
  与 Grafana 面板（`tools/grafana/quicx_dashboard.json`）；
- `docs/zh/design/` 16 篇模块设计文档 + 双语 reference / guide。

## 当前重点

1. **文档完备性**：补齐 `docs/en/reports/` 下的占位翻译（如
   `performance_baseline.md` 指向中文版的状态持续到补译完成）；
2. **教学纵深**：按 `LEARNING_PATH.md` 的阅读反馈补注释、补设计文档；
3. **性能透明**：维护 `scripts/perf/`（火焰图、内存分析）与
   `scripts/ci/perf_regression.sh` 的持续可用；
4. **示例质量**：精选 `example/` 与 README 联动，淘汰过时示例。

## 明确不做 / 暂缓

- 不做 SemVer / ABI 稳定承诺（`2.0` 前不讨论 C ABI）；
- Multipath / DATAGRAM / ACK Frequency 暂缓（见
  `docs/zh/reference/support_matrix.md` 的已知限制）。

---

*本文件保持短小：只记录方向与边界，执行细节一律回归 CHANGELOG 与各专题文档。*
