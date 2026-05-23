# QuicX 功能支持矩阵

> 适用于 **v0.1.x**。本文档是"**v0.1.x 实际支持哪些能力、哪些是部分支持、哪些刻意不实现**"的权威依据，每个 minor 版本发布时同步更新。
>
> 配套阅读：[`api_stability.md`](./api_stability.md)（API 稳定性策略）、
> [`reports/interop_status.md`](../reports/interop_status.md)（互操作性测试结果）、
> [`../../../CHANGELOG.md`](../../../CHANGELOG.md)（变更日志）。

如果你正在评估"能不能在生产环境用 QuicX"，请直接跳到本文末尾的
[**已知限制汇总**](#已知限制汇总采纳前请通读) —— 那里列出了你需要知道的所有"红线"。

---

## 图例

| 标记 | 含义 |
|:---:|---|
| ✅ | 已完整实现，单元测试覆盖，example 中实际使用 |
| 🟡 | 部分实现 —— 主流程可用；具体限制见"备注"列 |
| 🧪 | 已实现但属于实验性 —— API 或行为可能调整 |
| ❌ | 当前版本未实现 |
| 🚫 | 项目范围之外，明确不做 |

---

## QUIC 传输层（RFC 9000 / RFC 9369）

### 协议版本与协商

| 能力 | 状态 | 备注 |
|---|:---:|---|
| QUIC v1（`0x00000001`，RFC 9000） | ✅ | 默认版本 |
| QUIC v2（`0x6b3343cf`，RFC 9369） | ✅ | 可按连接选择 |
| Version Negotiation 包 | ✅ | 服务端发起 |
| 强制版本降级抵抗 | ✅ | 遵循 RFC 9000 §6 |

### 握手与 TLS

| 能力 | 状态 | 备注 |
|---|:---:|---|
| 基于 BoringSSL 的 TLS 1.3 | ✅ | |
| 1-RTT 握手 | ✅ | |
| 0-RTT 握手（Early Data） | ✅ | 重放保护遵循 RFC 9001 §9 |
| Session Ticket 缓存 | ✅ | 内存缓存；持久化由应用决定 |
| `SSLKEYLOGFILE`（用于 Wireshark 解密） | ✅ | |
| Retry 包（防放大攻击） | ✅ | `RetryPolicy::NEVER / SELECTIVE / ALWAYS` |
| 地址验证 token | ✅ | 包含无状态 Retry token |
| 证书验证 | ✅ | 客户端校验服务端证书 |
| 自定义验证回调 | ✅ | 通过 `QuicClientConfig` 注入 |
| 客户端证书（mTLS） | 🟡 | 代码路径已存在，仅有少量单元测试覆盖，无端到端 example |

### 连接生命周期

| 能力 | 状态 | 备注 |
|---|:---:|---|
| 单 UDP socket 上的多连接管理 | ✅ | |
| 优雅 `CONNECTION_CLOSE`（传输层 + 应用层） | ✅ | |
| Stateless Reset | ✅ | |
| 空闲超时 | ✅ | 通过 transport parameter 协商 |
| `PING` 帧保活 | ✅ | |

### 流与流量控制

| 能力 | 状态 | 备注 |
|---|:---:|---|
| 双向流 | ✅ | |
| 单向流 | ✅ | |
| 流级流量控制 | ✅ | |
| 连接级流量控制 | ✅ | |
| `STREAM_DATA_BLOCKED` / `DATA_BLOCKED` | ✅ | 收发都处理 |
| `MAX_STREAMS` / `STREAMS_BLOCKED` | ✅ | |
| `RESET_STREAM` / `STOP_SENDING` | ✅ | |

### 拥塞控制与丢包恢复

| 能力 | 状态 | 备注 |
|---|:---:|---|
| BBR v1 | ✅ | |
| BBR v2 | ✅ | |
| BBR v3 | 🧪 | 已实现，但调参仍在调整中 |
| CUBIC | ✅ | |
| Reno | ✅ | |
| 可插拔拥塞控制器（工厂模式） | ✅ | 按连接选择 |
| 数据包 Pacing | ✅ | |
| 基于 ACK 的丢包检测（RFC 9002） | ✅ | |
| PTO（Probe Timeout） | ✅ | |
| 按加密级别独立追踪重传 | ✅ | |
| ECN 标记与反馈 | 🟡 | 可选，默认关闭；部分仿真对端不验证 |

### 连接迁移（RFC 9000 §9）

| 能力 | 状态 | 备注 |
|---|:---:|---|
| 客户端主动迁移 | ✅ | |
| NAT 重绑定检测 | ✅ | |
| 路径验证（`PATH_CHALLENGE` / `PATH_RESPONSE`） | ✅ | |
| **迁移过程中的 CID 轮换** | ✅ | RFC 9000 §9.5：迁移成功后自动轮换到新的 remote CID，并触发对端 `RETIRE_CONNECTION_ID`；本端按 §19.16 单 seq 语义退役 + §19.15 批量退役 (retire_prior_to)，退役后自动补充本地池。verified by interop self-test (connectionmigration / rebind-port / rebind-addr 全 PASS) |
| 服务端发起的 `NEW_CONNECTION_ID` 轮换 | ✅ | 握手期间下发，迁移后/退役后按 `active_connection_id_limit` 自动补充 |
| `RETIRE_CONNECTION_ID` 处理 | ✅ | |

### 其他传输层能力

| 能力 | 状态 | 备注 |
|---|:---:|---|
| 密钥更新（RFC 9001 §6） | ✅ | 可选，自动 |
| `HANDSHAKE_DONE` 帧 | ✅ | |
| `NEW_TOKEN` 帧 | ✅ | |
| Multipath QUIC（`draft-ietf-quic-multipath`） | ❌ | 未实现 |
| DATAGRAM 帧（RFC 9221） | ❌ | 未实现 |
| ACK Frequency 扩展（`draft-ietf-quic-ack-frequency`） | ❌ | 未实现 |
| Reliable Stream Reset（`draft-ietf-quic-reliable-stream-reset`） | ❌ | 未实现 |
| Greasing（RFC 8701） | 🟡 | 帧类型 greasing 已做；transport parameter greasing 部分支持 |

---

## HTTP/3（RFC 9114）

### 协议核心

| 能力 | 状态 | 备注 |
|---|:---:|---|
| HTTP/3 帧（DATA / HEADERS / SETTINGS / GOAWAY 等） | ✅ | |
| 请求流 | ✅ | |
| 响应流 | ✅ | |
| Control 流 | ✅ | |
| `GOAWAY` 优雅关闭 | ✅ | |
| `SETTINGS` 交换 | ✅ | |
| 保留流类型忽略 | ✅ | |

### QPACK（RFC 9204）

| 能力 | 状态 | 备注 |
|---|:---:|---|
| 静态表 | ✅ | |
| 动态表（编码器 + 解码器） | ✅ | |
| Huffman 编 / 解码 | ✅ | |
| 编码器流 | ✅ | |
| 解码器流 | ✅ | |
| Insert count 与 stream blocking | ✅ | |

### 服务端能力

| 能力 | 状态 | 备注 |
|---|:---:|---|
| 路径参数路由（`/users/:id`） | ✅ | |
| 通配路由（`/static/*`） | ✅ | |
| 按 HTTP 方法注册 handler | ✅ | 支持全部标准动词 |
| Before / After 中间件链 | ✅ | 按方法生效 |
| 服务端推送（`PUSH_PROMISE`） | ✅ | |
| 通过 `IAsyncServerHandler` 流式接收请求体 | ✅ | |
| Trailers | 🟡 | 编码器侧已支持；路由层便利 API 较薄 |
| 1xx 信息响应（Early Hints） | ❌ | |

### 客户端能力

| 能力 | 状态 | 备注 |
|---|:---:|---|
| 同步风格的请求 / 响应 | ✅ | |
| 通过 `IAsyncClientHandler` 流式接收响应体 | ✅ | |
| Push Promise 接受 / 拒绝回调 | ✅ | |
| 跨主机连接池 | 🟡 | 同主机单连接复用；跨主机池由应用层处理 |

### HTTP 方法

| 方法 | 状态 |
|---|:---:|
| GET / HEAD / POST / PUT / DELETE | ✅ |
| OPTIONS / TRACE / PATCH | ✅ |
| CONNECT | ✅ |
| `CONNECT-UDP`（RFC 9298 / MASQUE） | ❌ |
| Extended CONNECT for WebTransport | ❌ |

---

## HTTP 升级

| 能力 | 状态 | 备注 |
|---|:---:|---|
| HTTP/1.1 → HTTP/3 `Alt-Svc` 通告 | ✅ | `src/upgrade` |
| 协商 example | ✅ | `example/upgrade_h3` |
| HTTP/2 → HTTP/3 回退 | 🚫 | 项目范围外（QuicX 自身不提供 HTTP/2 服务端） |

---

## 可观测性

| 能力 | 状态 | 备注 |
|---|:---:|---|
| 内置 Metrics 注册表 | ✅ | UDP / QUIC / HTTP/3 / 拥塞控制 / TLS / 迁移 / Retry / 内存 |
| Metrics HTTP 端点 | ✅ | 可选，通过 `Http3ServerConfig::metrics_` 配置 |
| QLog（RFC 9001 §A） | ✅ | 编译时加 `-DQUICX_ENABLE_QLOG=ON` |
| 分级日志 | ✅ | |
| OpenTelemetry 导出 | ❌ | 应用可基于 metrics 注册表自行桥接 |

---

## 平台

> 这里的"平台支持"指**构建系统支持的目标**。Linux/macOS/Windows 三平台的常态 CI 列在 v0.2.0 路线图；当前是"开发者验证过，但没有持续覆盖"。

| 平台 | 构建 | 运行 | 备注 |
|---|:---:|:---:|---|
| Linux x86_64（gcc 9+ / clang 10+） | ✅ | ✅ | 主开发平台 |
| Linux aarch64 | 🟡 | 🟡 | 应当能编；未做常态测试 |
| macOS x86_64 / arm64 | ✅ | ✅ | 网络层路径见 `src/common/network/macos` |
| Windows x86_64（MSVC 2019+） | ✅ | 🟡 | 网络层路径见 `src/common/network/windows`；冒烟时间少于 Linux |
| FreeBSD / OpenBSD | ❌ | ❌ | 未尝试 |
| 32 位目标 | 🚫 | 🚫 | 项目范围外 |

---

## 编译器与工具链

| 工具链 | 状态 | 备注 |
|---|:---:|---|
| GCC 9+ | ✅ | C++17 |
| Clang 10+ | ✅ | 含 ASan / UBSan / TSan / libFuzzer |
| MSVC 2019+ | 🟡 | 能编；缺 CI 覆盖 |
| Apple Clang | ✅ | |

---

## 构建系统

| 系统 | 状态 | 备注 |
|---|:---:|---|
| CMake ≥ 3.16 | ✅ | 主构建 |
| Bazel | 🟡 | `BUILD.bazel` 文件已存在；不如 CMake 久经考验 |
| Makefile | 🚫 | 不提供 |

---

## Sanitizer 与动态分析

| 工具 | 状态 | 备注 |
|---|:---:|---|
| AddressSanitizer | ✅ | 在 `quicx_utest` 上干净 |
| UndefinedBehaviorSanitizer | ✅ | 在 `quicx_utest` 上干净 |
| ThreadSanitizer | ✅ | 在 `quicx_utest` 上干净 |
| MemorySanitizer | 🟡 | 需要插桩过的 BoringSSL；不在常态验证之列 |
| libFuzzer（frame / packet / qpack / varint） | ✅ | `-DENABLE_FUZZING=ON`，冒烟干净 |
| Valgrind | 🟡 | 短时间运行可用；不在 CI 中 |

---

## 互操作性（仿真器）

互操作矩阵每次发布时重新生成，详细报告见
[`reports/interop_status.md`](../reports/interop_status.md)。

**v0.1.0 概要**：

- **`handshake`** 场景：与主流大多数对端通过（quinn、msquic、ngtcp2、neqo、lsquic、picoquic、quic-go、mvfst、aioquic）。
- **`transfer`** 场景：与多数对端通过；少量对端的已知问题记录在 [`../../internal/quic_interop_sim_issues.md`](../../internal/quic_interop_sim_issues.md)。
- 进阶场景（`multiconnect` / `resumption` / `keyupdate` / `chacha20` / `retry` / `zerortt` / `http3`）部分覆盖；逐对结果见互操作状态文档。

---

## 已知限制汇总（采纳前请通读）

1. **不支持 Multipath / DATAGRAM / ACK Frequency** —— 需要这些的应用不应采纳 v0.1.x。
2. **缺跨平台 CI** —— Windows 与 macOS 仅由开发者本地验证，没有持续覆盖。
3. **公有 API 在任何 `0.x` minor 之间都可能调整** —— 详见 [`api_stability.md`](./api_stability.md)。
4. **安全响应 SLA 仅"尽力而为"** —— 具体口径见 [`../../../SECURITY.md`](../../../SECURITY.md)。
5. **mTLS / Trailers / 连接池** 有可工作的代码，但端到端验证有限。

---

## 路线图指引

- **v0.2.0** —— Linux/macOS/Windows CI；DATAGRAM 帧（计划中）
- **v0.3.0** —— Multipath QUIC 调研；ACK Frequency
- **v1.0.0** —— API 冻结，SemVer 正式生效

更长期的视角见 [`../../internal/maturity_roadmap.md`](../../internal/maturity_roadmap.md)。
