# QuicX

<p align="left"><img width="500" src="./docs/image/logo.png" alt="quicX logo"></p>

<p align="left">
  <a href="https://opensource.org/licenses/BSD-3-Clause"><img src="https://img.shields.io/badge/license-BSD--3--Clause-orange.svg" alt="License"></a>
</p>

[English](./README.md)

**QuicX** 是一个基于 QUIC 协议（RFC 9000 / RFC 9369）的 C++17 HTTP/3 库。它提供了一个完整的传输与应用层协议栈——从 UDP 套接字和 TLS 1.3（基于 BoringSSL）贯穿 QUIC 流，直至 HTTP/3 路由、QPACK 头部压缩和服务端推送——无需依赖任何外部 HTTP 框架。

---

## 目录

- [功能特性](#功能特性)
- [架构概览](#架构概览)
- [文档与教程](#文档与教程)
- [示例](#示例)
- [可观测性](#可观测性)
- [测试](#测试)
- [许可证](#许可证)

---

## 功能特性

### QUIC 协议（RFC 9000 / RFC 9369）

| 功能域 | 说明 |
|---|---|
| **TLS** | 基于 BoringSSL 的 TLS 1.3；0-RTT / 1-RTT 握手；会话票据缓存；支持 SSLKEYLOGFILE |
| **协议版本** | QUIC v1 (`0x00000001`) 和 QUIC v2 (`0x6b3343cf`)，支持版本协商 |
| **连接管理** | 多连接管理；优雅的 `CONNECTION_CLOSE`；Retry 包防放大攻击 |
| **连接迁移** | 主动迁移（RFC 9000 §9）；NAT 重绑定检测；通过 `PATH_CHALLENGE` / `PATH_RESPONSE` 进行路径验证 |
| **流管理** | 双向流和单向流；流级别和连接级别的流量控制；`STREAM_DATA_BLOCKED` / `DATA_BLOCKED` 帧 |
| **拥塞控制** | BBR v1 / v2 / v3、CUBIC、Reno——通过工厂类按连接选择；内置数据包 Pacer |
| **丢包恢复** | 基于 ACK 的丢包检测；PTO（探测超时）；支持加密级别追踪的包重传 |
| **ECN** | 可选的 ECN 标记和处理 |
| **密钥更新** | 可选的自动密钥更新 |

### HTTP/3

| 功能域 | 说明 |
|---|---|
| **QPACK** | 静态表 + 动态表（RFC 9204）；Huffman 编解码 |
| **流** | 请求/响应流；服务端推送流（可选）；控制流；编码器/解码器流 |
| **路由** | 路径参数匹配（`:param`）；通配路由（`*`）；按 HTTP 方法注册处理器 |
| **中间件** | 支持 Before / After 中间件链，按 HTTP 方法生效 |
| **处理器模式** | **完整模式**——完整 body 缓冲后再调用处理器；**流式模式**——`IAsyncServerHandler` / `IAsyncClientHandler` 按块接收数据 |
| **HTTP 方法** | GET、HEAD、POST、PUT、DELETE、CONNECT、OPTIONS、TRACE、PATCH |
| **服务端推送** | `PUSH_PROMISE` 帧；客户端侧可选推送接受/拒绝回调 |
| **HTTP 升级** | HTTP/1.1 → HTTP/3 升级路径（`src/upgrade`） |

### 核心基础设施

| 组件 | 说明 |
|---|---|
| **内存管理** | 自定义 Slab 分配器（`NormalAlloter`）；池化的 `BufferChunk` 链；接近零拷贝的 I/O 路径 |
| **网络 I/O** | 跨平台 UDP I/O（`linux/`、`macos/`、`windows/`）；非阻塞事件循环 |
| **线程模型** | 单线程或多线程模式；可配置 Worker 线程数 |
| **定时器** | 分层时间轮，用于连接空闲、PTO 和应用层定时器 |
| **日志** | 分级日志（Null / Debug / Info / Warn / Error）；可配置输出路径 |
| **QLog** | 符合 RFC 9001 的 QLog 跟踪（可选，`-DQUICX_ENABLE_QLOG=ON`） |
| **指标** | 丰富的内置 Metrics 注册表——涵盖 UDP、QUIC、HTTP/3、拥塞控制、内存、TLS、连接迁移、Retry 等 |

---

## 架构概览

```
┌─────────────────────────────────────────┐
│         应用程序 / 示例                  │
├─────────────────────────────────────────┤
│   HTTP/3 层  (src/http3)                │
│   IClient / IServer  ←→  Router        │
│   QPACK  ·  Frames  ·  Push            │
├─────────────────────────────────────────┤
│   HTTP 升级层  (src/upgrade)             │
├─────────────────────────────────────────┤
│   QUIC 层  (src/quic)                   │
│   Connection  ·  Stream  ·  Crypto      │
│   拥塞控制  ·  丢包恢复                  │
│   Packet / Frame 编解码                  │
├─────────────────────────────────────────┤
│   公共组件  (src/common)                 │
│   Buffer  ·  Alloter  ·  Network I/O   │
│   Timer  ·  Log  ·  Metrics  ·  QLog   │
└─────────────────────────────────────────┘
```

---

## 文档与教程

为了帮助你快速集成并掌握 `quicX`，我们提供了完善的中英文开发者文档。有关集成方式、代码示例和底层核心配置调优，请参考下列指南：

### 快速入门指南
* [编译与构建指南](./docs/zh/getting-started/build.md) - 学习如何通过 CMake 或 Bazel 在各大平台编译 `quicX` 并将其引入你的项目中。
* [运行你的第一个程序](./docs/zh/getting-started/quick_start.md) - 编译并理解原生的 HTTP/3 客户端与服务端 `Hello World` 示例。

### 核心 API 与配置参考
* [HTTP/3 应用层 API 核心指南](./docs/zh/tutorial/http3_api_guide.md) - 开发 Web 服务端的开箱即用首选指南。包括路由引擎、中间件编排、大文件流式传输和服务端主动推送。
* [QUIC 传输层 API 核心指南](./docs/zh/tutorial/quic_api_guide.md) - 适用于开发私有 RPC 协议或游戏加速通道的开发者，深入了解核心的 QUIC Engine, Connection 和 Stream 的抽象。
* [配置项大全 (Configuration Reference)](./docs/zh/tutorial/configuration_reference.md) - 极其详尽的结构体字典。全方位解析流量控制（滑动窗口）、高并发限制、防 DDoS 攻击的 Retry 策略以及编译期配置。

---

## 示例

所有示例代码均位于 `example/` 目录下，并在 CMake 开启 `-DBUILD_EXAMPLES=ON` 时默认编译。

| 示例 | 说明 |
|---|---|
| `hello_world` | 最小化 GET 请求 / 响应 |
| `restful_api` | 带路径参数的 REST API |
| `file_transfer` | 使用流式处理器的大文件上传 / 下载 |
| `streaming_api` | 分块流式响应 |
| `bidirectional_comm` | 双向流通信 |
| `concurrent_requests` | 多并发请求 |
| `connection_lifecycle` | 连接事件与优雅关闭 |
| `error_handling` | 协议错误处理模式 |
| `server_push` | HTTP/3 服务端推送（`PUSH_PROMISE`） |
| `load_testing` | 简单负载生成 |
| `performance_benchmark` | 吞吐量 / 延迟基准测试 |
| `metrics_monitoring` | 运行时读取内置指标 |
| `qlog_integration` | 生成用于 Wireshark / qvis 的 QLog 跟踪文件 |
| `upgrade_h3` | HTTP/1.1 → HTTP/3 升级 |
| `quicx_curl` | 类 curl 命令行客户端 |

---

## 可观测性

### 内置 Metrics

QuicX 内置了覆盖以下维度的 Metrics 注册表：

- **UDP 层**：收发包 / 字节数、丢包、发送错误
- **QUIC 连接**：活跃连接数、总量、握手成功 / 失败次数、时长直方图
- **QUIC 数据包**：收发、重传、丢失、丢弃、确认
- **QUIC 流**：活跃数、创建 / 关闭数、收发字节、RESET 帧统计
- **流量控制**：阻塞事件次数
- **HTTP/3**：请求总量 / 活跃数 / 失败数、时延直方图、推送承诺、状态码分桶（2xx/3xx/4xx/5xx）
- **拥塞控制**：拥塞窗口、事件次数、慢启动退出次数、在途字节、Pacing 速率
- **延迟**：平滑 RTT、RTT 方差、最小 RTT、包处理时间、ACK 延迟
- **内存**：池分配次数、空闲 / 已用块数
- **TLS**：握手时长、会话恢复 / 缓存次数
- **连接迁移**：总次数、失败次数
- **Retry**：发送次数、触发原因、Token 验证情况

通过 `MetricsRegistry` 在运行时读取指标，或借助 `Http3ServerConfig::metrics_` / `Http3ClientConfig::metrics_` 中配置的可选 HTTP 指标端点对外暴露。

### QLog

使用 `-DQUICX_ENABLE_QLOG=ON` 编译并在 `QuicConfig::qlog_config_` 中配置输出路径即可启用。生成的跟踪文件兼容 [qvis](https://qvis.quictools.info/) 和 Wireshark。

---

## 测试

```bash
# 单元测试
./build/bin/quicx_utest

# 集成测试（需要本地 server / client）
python3 run_tests.py

# 拥塞控制模拟器
./build/bin/cc_simulator

# 模糊测试（需要 Clang + libFuzzer）
cmake -B build_fuzz -DENABLE_FUZZING=ON -DCMAKE_CXX_COMPILER=clang++
cmake --build build_fuzz
```

---

## 许可证

BSD 3-Clause License — 详见 [LICENSE](LICENSE) 文件。
