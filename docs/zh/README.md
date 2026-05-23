# QuicX 文档（中文）

这是中文文档树。English 镜像见 [`../en/README.md`](../en/README.md)。
仓库入口：[`../../README.md`](../../README.md)（English） ·
[`../../README_cn.md`](../../README_cn.md)（中文）。

---

## 1. 入门

| 主题 | 链接 |
| :--- | :--- |
| 构建与集成（CMake / Bazel、`add_subdirectory` / `find_package`） | [`getting-started/build.md`](getting-started/build.md) |
| 跑通你的第一个 HTTP/3 hello world | [`getting-started/quick_start.md`](getting-started/quick_start.md) |

## 2. 教程

| 主题 | 链接 |
| :--- | :--- |
| HTTP/3 应用层 API（路由、中间件、推送、流式 body） | [`tutorial/http3_api_guide.md`](tutorial/http3_api_guide.md) |
| QUIC 传输层 API（裸流 / 自建 RPC 隧道） | [`tutorial/quic_api_guide.md`](tutorial/quic_api_guide.md) |
| 配置项参考（`QuicConfig` / `Http3Config`） | [`tutorial/configuration_reference.md`](tutorial/configuration_reference.md) |

## 3. 操作指南

操作手册类内容（"怎么做"），非契约、非教程。

| 主题 | 链接 |
| :--- | :--- |
| 性能测试与剖析工具 | [`guide/perf_testing.md`](guide/perf_testing.md) |
| CI 本地调试（与 GitHub Actions 同构） | [`guide/ci_local.md`](guide/ci_local.md) |
| 互通测试框架原理（`quic-interop-runner`） | [`guide/interop_overview.md`](guide/interop_overview.md) |
| 互通测试运行手册（命令与场景） | [`guide/interop_runbook.md`](guide/interop_runbook.md) |

> 注：`perf_testing.md` 和 `ci_local.md` 目前仅提供中文版；英文版为占位文档，待后续翻译。

## 4. 参考（发布契约）

可被下游项目依赖的权威文档，更新频率低。

| 文档 | 内容 |
| :--- | :--- |
| [`reference/support_matrix.md`](reference/support_matrix.md) | 功能支持矩阵；平台、工具链、Sanitizer |
| [`reference/api_stability.md`](reference/api_stability.md) | 公有头清单与 API 稳定性策略 |
| [`reference/qlog_event_coverage.md`](reference/qlog_event_coverage.md) | qlog 事件覆盖清单（已实现 / 未覆盖） |

发布说明和安全策略在仓库根：
[`../../CHANGELOG.md`](../../CHANGELOG.md) ·
[`../../SECURITY.md`](../../SECURITY.md) ·
[`../../CONTRIBUTING.md`](../../CONTRIBUTING.md)。

## 5. 测试与基准报告

带时间戳的结果快照，每轮测试后会被新版本取代——**非契约**。

| 文档 | 范围 |
| :--- | :--- |
| [`reports/interop_status.md`](reports/interop_status.md) | 与外部 QUIC 实现的最新互通测试结果 |
| [`reports/performance_baseline.md`](reports/performance_baseline.md) | 性能基准线（CPU 热点、Buffer / Frame / Packet 吞吐） |

> 注：`performance_baseline.md` 目前仅提供中文版；英文版为占位文档，待后续翻译。

## 6. 设计文档

集成或扩展 QuicX 时值得了解的内部约定。这一节不是讨论"未来要做什么"的 RFC，
而是描述**当前代码已有的不变量**。

| 主题 | 链接 |
| :--- | :--- |
| 内置 Metrics 名录 | [`design/metrics.md`](design/metrics.md) |
| Buffer / 连接所有权模型 | [`design/ownership_and_memory.md`](design/ownership_and_memory.md) |

## 7. 内部（仅维护者）

非用户契约的工作文档（路线图、代码评审、性能调查、互通排查）。
集成方与终端用户可忽略。详见 [`../internal/README.md`](../internal/README.md)。
