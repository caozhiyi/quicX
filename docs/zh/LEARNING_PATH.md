# QuicX 源码学习路径（Learning Path）

QuicX 是一套**面向生产**的 QUIC / HTTP/3 协议栈：1523 个单元与集成测试、干净的 ASan / UBSan / TSan 运行、90.60% 的互通矩阵通过率；代码同时保持可读、注释完整、测试与文档自洽。自 1.0 起，公有 C++ API 遵循语义化版本（SemVer）——详见 [`api_stability.md`](./reference/api_stability.md)。

本文是阅读源码的**总入口**：按「一个包从网卡到 HTTP/3 handler 的完整旅程」排序，每一站都给出源码目录、配套设计文档与动手实验。全部路径均相对仓库根目录。

---

## 阅读前的准备

1. 通读 `README.md`（或 `README_cn.md`）的架构图与特性表，建立全局印象。
2. 准备构建环境，跑通最小示例 `example/hello_world/`：
   ```bash
   mkdir build && cd build && cmake .. && make -j
   # 然后按 example/hello_world/ 内的 README / 注释启动 client 与 server
   ```
3. 跑一遍测试，确认环境健康：`python3 run_tests.py`（单测二进制位于
   `build/bin/quicx_utest`）。

**总原则**：每一站先读设计文档建立心智模型，再进源码对照，最后用对应单测
（`test/unit_test/`）验证理解。

---

## 第 0 站：示例与公有 API —— `example/` 与 `include/quicx/`

- **目标**：知道「使用者视角」的 QuicX 长什么样。
- **源码入口**：`example/hello_world/`（最小可运行 client/server）。
- **API 全集**：`include/quicx/` 下的 `common/`、`quic/`、`http3/`、`upgrade/`
  四个公有头目录（权威清单见 [`api_stability.md`](./reference/api_stability.md)）。
- **动手**：修改 hello_world，加一条自定义 HTTP 路由 / 打印一条 stream 数据。

## 第 1 站：线程与事件模型 —— `src/quic/quicx/`、`src/common/thread/`

- **目标**：理解 worker / 事件循环如何组织（QuicX 的骨架）。
- **源码入口**：`src/quic/quicx/`（worker 与调度）、`src/common/thread/`。
- **配套文档**：[`design/process_model.md`](./design/process_model.md)。

## 第 2 站：UDP I/O —— `src/quic/udp/`

- **目标**：包如何真正进出网卡；收发缓冲如何衔接。
- **源码入口**：`src/quic/udp/`（UDP socket 的读写与分发）。
- **配套文档**：[`design/udp_io.md`](./design/udp_io.md)。
- **动手**：用 `test/unit_test/` 中 udp 相关用例观察分包与合包行为。

## 第 3 站：包解析 —— `src/quic/packet/`

- **目标**：QUIC 长短头部、版本协商、Initial / 0-RTT / Handshake / 1-RTT
  各包型的编解码。
- **源码入口**：`src/quic/packet/`，从 `packet_decode.cpp` 的
  `DecodePackets` 入口顺着类型分派往下读。
- **配套文档**：[`design/packet_lifecycle.md`](./design/packet_lifecycle.md)。
- **实验**：`test/perf/packet_perf_test.cpp` 提供各包型的编解码基准；
  `docs/internal/perf_flamegraph_analysis.md` 记录了一次真实的
  「dispatch 路径 195× 性能悬案」排查全过程，强烈推荐一读。

## 第 4 站：加密与握手 —— `src/quic/crypto/`

- **目标**：TLS 1.3（BoringSSL）如何被嵌入 QUIC：Initial 密钥派生、
  0-RTT / 1-RTT 密钥调度、握手状态机。
- **源码入口**：`src/quic/crypto/`。
- **配套文档**：[`design/crypto_keying.md`](./design/crypto_keying.md)、
  [`design/handshake_state_machine.md`](./design/handshake_state_machine.md)。

## 第 5 站：帧编解码 —— `src/quic/frame/`

- **目标**：STREAM / ACK / CRYPTO / FLOW_CONTROL 等帧的结构与编解码，
  以及帧 → 流 / 连接状态的映射。
- **源码入口**：`src/quic/frame/`（每种帧一个类，逐个对照 RFC 9000 阅读）。

## 第 6 站：流与流量控制 —— `src/quic/stream/`

- **目标**：双向 / 单向流的状态机、发送与接收缓冲、流级与连接级流控。
- **源码入口**：`src/quic/stream/`。
- **配套文档**：[`design/stream_state_machine.md`](./design/stream_state_machine.md)。

## 第 7 站：连接与丢包恢复 —— `src/quic/connection/`

- **目标**：连接对象如何串起上述所有模块：包构造、ACK 处理、
  PTO / 丢包判定与重传。
- **源码入口**：`src/quic/connection/`。
- **配套文档**：[`design/connection_anatomy.md`](./design/connection_anatomy.md)、
  [`design/loss_recovery.md`](./design/loss_recovery.md)。

## 第 8 站：拥塞控制 —— `src/quic/congestion_control/`

- **目标**：Reno 系拥塞窗口演化、pacing、与丢包恢复的联动。
- **源码入口**：`src/quic/congestion_control/`。
- **配套文档**：[`design/congestion_control.md`](./design/congestion_control.md)。
- **实验**：`test/congestion_control/` 与 `test/perf/congestion_control_perf_test.cpp`。

## 第 9 站：HTTP/3 —— `src/http3/`

- **目标**：控制流与请求流、QPACK 之外的帧体系（HEADERS / DATA / SETTINGS /
  GOAWAY…）、路由与服务端推送。
- **源码入口**：`src/http3/frame/`（帧）、`src/http3/stream/`（流）、
  `src/http3/router/`（路由）、`src/http3/http/`（HTTP 语义）。
- **配套文档**：[`design/h3_connection.md`](./design/h3_connection.md)。

## 第 10 站：QPACK —— `src/http3/qpack/`

- **目标**：头部压缩的静态 / 动态表、编码器 / 解码器指令流。
- **源码入口**：`src/http3/qpack/`。
- **配套文档**：[`design/qpack_dynamic_table.md`](./design/qpack_dynamic_table.md)。

---

## 横切关注点（随时穿插阅读）

| 主题 | 源码 | 设计文档 |
|---|---|---|
| 缓冲区抽象 | `src/common/buffer/` | — |
| 定时器（TreeMapTimer） | `src/common/timer/` | [`design/timer_design.md`](./design/timer_design.md) |
| 内存池 / slab 分配 | `src/common/allocator/`、`src/common/structure/` | [`design/pool_allocator.md`](./design/pool_allocator.md)、[`design/ownership_and_memory.md`](./design/ownership_and_memory.md) |
| 日志 | `src/common/log/` | — |
| 指标（/metrics Prometheus 端点） | `src/common/metrics/`、`src/http3/metric/` | [`design/metrics.md`](./design/metrics.md) |
| qlog | `src/common/qlog/` | — |
| 0-RTT / Retry / 升级协商 | `src/quic/`、`src/http3/` | [`design/upgrade_negotiation.md`](./design/upgrade_negotiation.md) |

## 验证与进阶

1. **单测**：`test/unit_test/`（约 1500+ 用例），按模块挑读，看行为契约。
2. **基准**：`test/benchmarks/` 与 `test/perf/`；火焰图工具见
   `scripts/perf/generate_flamegraph.sh` 与
   `docs/internal/perf_flamegraph_analysis.md`。
3. **互通**：QuicX 与 17 个主流 QUIC 实现的 24 场景互通结果与跑测手册：
   [`reports/interop_status.md`](./reports/interop_status.md)、
   [`guide/interop_runbook.md`](./guide/interop_runbook.md)。
4. **运维面板**：`/metrics` 端点 + `tools/grafana/quicx_dashboard.json`。

> 发现文档与代码不一致、或想改进某站的讲解？欢迎按 `CONTRIBUTING.md` 提
> issue / PR。
