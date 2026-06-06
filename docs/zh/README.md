# QuicX 文档（中文）

本目录是 QuicX 的**文档地图**——按职责把 `docs/zh/` 下的所有文档分类列出，并指出每篇文档解答什么问题。它本身不教你怎么用 QuicX，使用入口请走仓库根的 README 与第 1、2 节。

---

## 1. 入门（`getting-started/`）

最小路径：先编出 QuicX，再跑出第一个 HTTP/3 hello world。

| 文档 | 解答什么问题 |
| :--- | :--- |
| [`getting-started/build.md`](getting-started/build.md) | 如何用 CMake / Bazel 集成 QuicX，`add_subdirectory` 与 `find_package` 两种接入方式 |
| [`getting-started/quick_start.md`](getting-started/quick_start.md) | 如何跑出第一个 HTTP/3 hello world，应该看到什么现象 |

## 2. 教程（`tutorial/`）

写第一个非 demo 程序时需要的三篇 API 走查。

| 文档 | 解答什么问题 |
| :--- | :--- |
| [`tutorial/http3_api_guide.md`](tutorial/http3_api_guide.md) | HTTP/3 应用层 API：路由、中间件、Server Push、流式 body |
| [`tutorial/quic_api_guide.md`](tutorial/quic_api_guide.md) | QUIC 传输层 API：裸流、自建 RPC 隧道 |
| [`tutorial/configuration_reference.md`](tutorial/configuration_reference.md) | `QuicConfig` / `Http3Config` 各参数含义与默认值 |

## 3. 操作指南（`guide/`）

操作手册类内容（"怎么做"）——非契约、非教程，按需查阅。

| 文档 | 解答什么问题 |
| :--- | :--- |
| [`guide/perf_testing.md`](guide/perf_testing.md) | 性能测试与剖析工具的使用 |
| [`guide/ci_local.md`](guide/ci_local.md) | 在本地复现 GitHub Actions CI 的同构环境 |
| [`guide/interop_overview.md`](guide/interop_overview.md) | `quic-interop-runner` 互通测试框架的工作原理 |
| [`guide/interop_runbook.md`](guide/interop_runbook.md) | 互通测试运行手册：命令与场景 |
| [`guide/sanitizer_hello_world_load.md`](guide/sanitizer_hello_world_load.md) | Sanitizer 场景：hello_world 加压 |
| [`guide/sanitizer_file_transfer.md`](guide/sanitizer_file_transfer.md) | Sanitizer 场景：file_transfer |

> 注：`perf_testing.md` 和 `ci_local.md` 目前仅提供中文版；英文版为占位文档，待后续翻译。

## 4. 参考（`reference/`）

可被下游项目依赖的权威文档，更新频率低。

| 文档 | 解答什么问题 |
| :--- | :--- |
| [`reference/support_matrix.md`](reference/support_matrix.md) | 平台、工具链、Sanitizer 支持矩阵 |
| [`reference/api_stability.md`](reference/api_stability.md) | 公有头清单与 API 稳定性策略 |
| [`reference/qlog_event_coverage.md`](reference/qlog_event_coverage.md) | qlog 事件覆盖清单（已实现 / 未覆盖） |

发布说明与安全策略在仓库根：
[`../../CHANGELOG.md`](../../CHANGELOG.md) ·
[`../../SECURITY.md`](../../SECURITY.md) ·
[`../../CONTRIBUTING.md`](../../CONTRIBUTING.md)。

## 5. 测试与基准报告（`reports/`）

带时间戳的结果快照，每轮测试后会被新版本取代——**非契约**。

| 文档 | 范围 |
| :--- | :--- |
| [`reports/interop_status.md`](reports/interop_status.md) | 与外部 QUIC 实现的最新互通测试结果 |
| [`reports/performance_baseline.md`](reports/performance_baseline.md) | 性能基准线（CPU 热点、Buffer / Frame / Packet 吞吐） |

> 注：`performance_baseline.md` 目前仅提供中文版；英文版为占位文档，待后续翻译。

## 6. 设计文档（`design/`）

集成或扩展 QuicX 时值得了解的内部约定。这一节不是讨论"未来要做什么"的 RFC，而是描述**当前代码已有的不变量**。

文档量较大，按职责分为五组。

### 6.1 主链路

理解一个 datagram / 一条连接 / 一次握手怎么被处理。

| 文档 | 解答什么问题 |
| :--- | :--- |
| [`design/packet_lifecycle.md`](design/packet_lifecycle.md) | 一个 datagram 从 socket 入到上层 frame 的完整路径 |
| [`design/connection_anatomy.md`](design/connection_anatomy.md) | Connection 子树（21 个 cpp）的三层结构：骨架 / 协调器 / 控制器 |
| [`design/handshake_state_machine.md`](design/handshake_state_machine.md) | TLS / 加密级别 / Key Update 的状态机 |
| [`design/ownership_and_memory.md`](design/ownership_and_memory.md) | Buffer / 连接 / 流的所有权与生命周期 |

### 6.2 关键决策

涉及"为什么这么做"的算法、协议、优化取舍。

| 文档 | 解答什么问题 |
| :--- | :--- |
| [`design/loss_recovery.md`](design/loss_recovery.md) | PTO / loss timer / ACK 处理（RFC 9002） |
| [`design/congestion_control.md`](design/congestion_control.md) | Reno / Cubic / BBR v1/v2/v3 实现取舍与可插拔机制 |
| [`design/qpack_dynamic_table.md`](design/qpack_dynamic_table.md) | QPACK 动态表与编 / 解码流的多流协作 |

### 6.3 基础设施

支撑主链路的底层机制，按需查阅。

| 文档 | 解答什么问题 |
| :--- | :--- |
| [`design/process_model.md`](design/process_model.md) | master + worker 进程模型、跨线程通道、为什么不用线程池 |
| [`design/timer_design.md`](design/timer_design.md) | 时间轮 vs treemap 双层定时器的取舍 |
| [`design/pool_alloter.md`](design/pool_alloter.md) | frame-level 内存池为什么需要、与现有优化互补 |
| [`design/udp_io.md`](design/udp_io.md) | GSO / sendmmsg / recvmmsg 取舍与降级路径 |

### 6.4 协议层细节

按 RFC 章节走查的协议细节。

| 文档 | 解答什么问题 |
| :--- | :--- |
| [`design/stream_state_machine.md`](design/stream_state_machine.md) | 流的收 / 发双状态机（RFC 9000 §3） |
| [`design/crypto_keying.md`](design/crypto_keying.md) | TLS 密钥派生与 Key Update（RFC 9001 §5/§6） |
| [`design/h3_connection.md`](design/h3_connection.md) | H3 控制流 / QPACK 编 / 解码流的多流协作 |
| [`design/upgrade_negotiation.md`](design/upgrade_negotiation.md) | H1→H3 协商、Alt-Svc、Upgrade 协议头交互 |

### 6.5 可观测性

| 文档 | 解答什么问题 |
| :--- | :--- |
| [`design/metrics.md`](design/metrics.md) | 内置 Metrics 名录与 emit 点 |

## 7. 进一步阅读源码

文档之外，源码本身就是最好的参考：

- **`example/`** 与 **`test/`** 是"可执行文档"——`example/hello_world` 看接入、`test/quic/` 看协议各模块单测、`tools/cc_simulator/` 看拥塞控制可视化。
- 仓库目录结构本身就是 index：`src/quic/` 下每个子目录与 RFC 9000 / 9001 / 9002 的章节大致对应，按需 `cd` 进去即可。
- 关键决策点（拥塞控制 / 丢包恢复 / 流控 / 握手 / QPACK）的源码已就近补 RFC §-级注释，看不懂直接查注释指向的条款。
