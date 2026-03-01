# 配置项大全 (Configuration Reference)

在 `quicX` 中，配置体系被精心划分为两层：**QUIC 基础传输层配置** 和 **HTTP/3 应用层配置**。这种设计允许你根据需要对网络行为进行极其细粒度的控制。

本文档完整地列出了两层的配置说明，以及它们的默认值和适用场景。无论你是想要压榨出极致的吞吐量，还是想要降低长连接的空闲资源占用，都能在这里找到答案。

---

## 一、 QUIC 传输层配置

如果你使用纯 QUIC 接口，则直接操作下列结构体；如果你使用 HTTP/3 接口，下列结构体被嵌套在 `Http3Config::quic_config_` 以及 `Http3Settings` 中。

### 1. `QuicConfig`：全局运行时与安全机制

它主要决定事件循环、加密解密和日志这种“全局性质”的行为。

| 字段名称 / 类型 | 默认值 | 全局意义与调优建议 |
| :--- | :--- | :--- |
| `thread_mode_`<br>`ThreadMode` | `kSingleThread` | **核心**：控制引擎的多线程架构。<br/>- `kSingleThread`：极致的单核低延迟，无锁切换开销。<br/>- `kMultiThread`：适合现代多核 CPU 的高并发服务器。底层的 UDP 包会被哈希分配到不同的 Worker 上。 |
| `worker_thread_num_`<br>`uint16_t` | `2` | 当模式为 `kMultiThread` 时，启动多少个 Worker 线程。建议设置为 `CPU核数 - 1`，留一个核给操作系统调度网络中断。 |
| `log_level_`<br>`LogLevel` | `kNull` | 日志级别控制。为了极限性能默认静默。调试时可设为 `kInfo` 或 `kDebug`。 |
| `quic_version_`<br>`uint32_t` | `kQuicVersion2` | 优先协商的协议版本。默认直接采用最新的 QUIC v2 (RFC 9369)。你可以手动降级为 v1。 |
| `enable_0rtt_`<br>`bool` | `false` | **性能**：开启后，如果客户端以前和服务器连过且持有票据，它可以**在握手完成前**就把第一个 HTTP 请求发出去！省去 1RTT 延迟，极其适合无连接感知的 API (例如 REST 接口)。 |
| `keylog_file_`<br>`std::string` | `""` | **极度重要调试选项**：开启后（传入文件路径）， `quicX` 会把对每个客户端加密用的 TLS 密钥倒出这个日志文件。可以结合 Wireshark 实现明文解析和流溯源。 |

### 2. `QlogConfig`：网络跟踪与诊断分析
在 `QuicConfig` 中，`qlog_config_` 是一个极其重要的内核诊断开关。开启后，程序将按照 RFC 9001 规范将每一帧数据流转输出为结构化日志（兼容前端可视化工具 `qvis` 和 `Wireshark`）。

| 字段名称 / 类型 | 默认值 | 功能意义与调优建议 |
| :--- | :--- | :--- |
| `enabled` | `false` | 是否开启 qlog 搜集。由于此选项极度拉低极限吞吐量，建议仅在排查丢包或者拥塞控制算法行为时打开。 |
| `output_dir` | `"./qlogs"` | 日志文件输出的根目录。 |
| `format` | `kSequential` | 文件格式。默认使用 `kSequential`（单行 JSON）以支持流式写入和防止爆内存。 |
| `batch_write` | `true` | 是否开启批处理异步写盘。生产环境下需要抓包**必须开启**以防磁盘 I/O 阻塞核心事件循环。 |
| `flush_interval_ms` | `100` | 如果 `batch_write` 开启，则每隔多少毫秒将缓冲池里的日志强制落盘。 |
| `max_file_size_mb` | `100` | 单个 Qlog 文件的上限大小。超过此阈值将自动拆分新文件（以防撑爆硬盘）。 |
| `max_file_count` | `10` | 系统内保留的历史文件总数，超出则自动滚动删除。 |

### 3. `QuicServerConfig` 扩展：防 DDoS 与 Retry 机制
如果你实例化的是 Server (`IQuicServer` 或 `quicx::IServer`)，你可以通过 `QuicServerConfig` 配置 `quicX` 的防御护城河。基于 RFC 9000 规范，`Retry` 包旨在防御源地址伪造的 UDP 放大攻击。

| 字段名称 | 默认值 | 功能意义与调优建议 |
| :--- | :--- | :--- |
| `retry_policy_` | `SELECTIVE` | **防御测略**：<br/>- `NEVER`: 永远不发送 Retry（性能最好，适合纯内网可信环境）。<br/>- `SELECTIVE`: (推荐) 当发现新连接频率陡增或某个 IP 请求频繁时，动态开启防伪造验证。<br/>- `ALWAYS`: 永远要求任何连入的客户端进行额外一轮握手验证（最安全但增加了 1-RTT 延迟）。 |
| `retry_token_lifetime_` | `60` (秒) | 派发给客户端用于证明 "它真的是这个IP" 的令牌的有效时间。 |
| `selective_retry_config_.rate_threshold_` | `1000` | (仅 `SELECTIVE` 模式生效) 全局新连接速率阈值 (连接/秒)。如果系统一秒内收到了极多新连接导致超过此速率，开始全局派发 Retry 进行清洗。 |
| `selective_retry_config_.ip_rate_threshold_` | `100` | (仅 `SELECTIVE` 模式生效) 单一 IP 速率阈值 (连接/分钟)。超过此速率的 IP 会被记录，单独拉出来强制验证真实性。 |

### 4. `QuicTransportParams`：传输参数协商字典

这些配置会在握手阶段通过 TLS 扩展打包发给对面，主要用于控制**滑动窗口（流量控制）**。

> [!WARNING]
> 一定要理解 QUIC 的双重限流控制。如果因为你没有调大这些窗口，就算物理带宽有 10Gbps，上层也会因为“对端告诉你窗口满了不能发”而被限制在 kb 级别。

| 字段名称 | 默认值 | 详细机制说明 |
| :--- | :--- | :--- |
| **`max_idle_timeout_ms_`** | `120000` (2分钟) | 超过这个时间没有任何包（应用包或 PING）交互，连接强制断开。物联网设备可以适当调长。 |
| **`initial_max_data_`** | `64 MB` | **连接级流量控制**。整个连接下所有流累加能发多少字节，发完之后只有等对方发了 Window_Update 更新窗口才能继续发。**大文件传输的瓶颈点 1**。 |
| **`initial_max_stream_data_bidi_local_`** | `16 MB` | **本地发起的双向流限流**。对每个具体的子 Stream，你最多能发多少。**大文件传输的瓶颈点 2**。 |
| **`initial_max_streams_bidi_`** | `200` | 最多允许对方同时发起多少个并行的双向流通道（HTTP/3 中代表并发请求数）。如果是海量请求的高并发微服务，可能需要调到 1000 以上。 |
| **`ack_delay_exponent_ms_`** | `3` | 计算 RTT 时的 ACK 延迟乘数因子。没特殊学术研究需求一般不用动。 |
| **`max_ack_delay_ms_`** | `25` | 最多等多久必须回一个 ACK 给对方。调大能省一点包，但可能导致发送方误认为丢包从而提前触发 PTO。 |

### 5. `MigrationConfig`：高级特性 连接迁移

QUIC 的招牌特性，用以实现真正的“无损移动网络切换”。

| 字段名称 | 默认值 | 功能意义 |
| :--- | :--- | :--- |
| `enable_active_migration_` | `true` | （客户端）主动迁移。客户端检测到本机网卡变换时，主动拿着原来的 Connection_ID 发包给服务器验证新路。 |
| `enable_nat_rebinding_` | `true` | （服务端）被动映射复活。当客户在网吧或使用 4G/5G，运营商的 NAT 端口突然老化改变时，服务器只要核对包没问题，就自动刷新映射关系不断线。 |
| `path_validation_timeout_ms_`| `6000` (6秒) | 老路断了，新路径握手探活（Path Challenge / Response）如果超过这个时间还没回应，彻底挂断。 |

---

## 二、 HTTP/3 层特定配置

HTTP/3 的这部分配置位于 `Http3Config`、`Http3ServerConfig` 与 `Http3ClientConfig` 中。由于 HTTP/3 的请求本身就是建立在 QUIC 的流之上，所以它的限制主要集中在防止服务端被恶意请求压垮上。

### 1. 并发与限流

| 字段名称 / 类型 | 默认值 | 应用层调优建议 |
| :--- | :--- | :--- |
| `max_concurrent_streams_`<br>`uint64_t` | `200` | **非常核心！** 这代表 HTTP/3 服务器能承受的单个客户端同时未处理完的并发请求数。一旦超限，新发起的请求会被阻塞。对于微服务网关或内网高并发聚合节点，你可能需要把它调到 1000 以上。 |
| `connection_timeout_ms_`<br>`uint32_t` | `0` (永不) | （客户端专有）当 `IClient::DoRequest` 拨号或发送的过程中，如果超过这个毫秒数没响应，则上报失败。针对公网弱网环境通常设定 3000~5000。 |

### 2. HTTP/3 专属进阶特性

| 字段定义 | 默认值 | 作用解析 |
| :--- | :--- | :--- |
| `enable_push_` | `false` | 是否开启 HTTP/3 Server Push 机制（RFC 9114）。开启后服务端可通过 `Response::AppendPush` 向客户端塞入未请求的静态资源（如 `style.css`、大图片）。在带宽抢占期有可能会产生副作用，目前处于试验开放阶段。 |
| `qpack_max_table_capacity` | `0` (动态表关闭) | 被存在于 `Http3Settings` 中。HTTP/3 头部压缩字典的大小。设置为 0 时系统使用纯静态字典（性能最好、内存完全无额外分配）；如果你传输的请求中带有海量长字符串自定义 Header（如各种 TraceID/Cookie），则应该把这个调大（以增加内存换取网络带宽节省）。 |

### 3. Metrics 端点

QUIC 栈会搜集非常详尽的关键统计信息（丢包率、重传数、缓冲池大小），你可以通过挂载内置端点导出。

```cpp
quicx::Http3ServerConfig server_config;
server_config.metrics_.prometheus_export = true;        // 是否使用 Prometheus 格式输出
server_config.metrics_.prometheus_endpoint = "/metrics"; // 直接在你的 quicX 进程开启 /metrics 路由
```
通过上述配置后，你的运维监控系统即可直连 `/metrics` 来观测 `quicX` 的运行情况了。

---

## 三、 编译期静态配置 (`config.h`)

除了可以在运行时（Runtime）动态传入的配置外，为了追求极致的性能和内存对齐，`quicX` 将一部分极其底层的基础阈值采用了 C++ `constexpr` 常量硬编码的形式。这些文件位于源码目录中。

如果你试图将 `quicX` 部署在有着极度苛刻资源限制的嵌入式设备，或者有着万兆网卡的顶级服务器上，你可能需要在**编译之前**修改它们：

### 1. HTTP/3 层编译期配置 (`src/http3/config.h`)

| 常量名称 | 默认值 | 作用解析 |
| :--- | :--- | :--- |
| `kMaxDataFramePayload` | `1350` | HTTP/3 层 DATA 帧单次投递给传输层的最大切片大小。1350 是为了完美塞入标准 1500 MTU（减去 IP头/UDP头/QUIC头/AEAD Tag/H3 帧头）。 |
| `kServerPushWaitTimeMs` | `30000` | 客户端等待服务端 Push 流到达的最长忍耐毫秒数（30秒）。 |
| `kClientConnectionTimeoutMs` | `60000` | 客户端 HTTP/3 会话层的空闲超时时间（60秒）。 |

### 2. QUIC 层编译期配置 (`src/quic/config.h`)

这份配置包含了协议的核心控制配置：

| 常量名称 | 默认值 | 作用说明与极客建议 |
| :--- | :--- | :--- |
| **滑动窗口与拥塞补充水线** | |
| `kDataBlockedThreshold` | `16384` (16KB) | 当发送方的全局窗口只剩这么点时，主动向对方发送 `DATA_BLOCKED` 帧。 |
| `kDataIncreaseThreshold` | `512 * 1024` (512KB) | 接收方发现目前可用窗口小于此值时，触发 `MAX_DATA` 帧告诉发送方“我有空余了，继续发”。如果万兆网卡，此值应成倍扩大以防发送方断流。 |
| `kDataIncreaseAmount` | `2 * 1024 * 1024` (2MB) | 上面触发更新后，一次性给对方扩充多大的流控窗口额度。对于超大文件专线传输，可以把这个写死到 10MB 以上。 |
| `kStreamWindowIncrement` | `2 * 1024 * 1024` | 针对单个 Stream 的扩充额度。 |
| **底层包与内存池限制** | |
| `kMaxFramePayload` | `1420` | QUIC 单个普通帧的最大长度。 |
| `kPacketPoolSize` | `256` | 数据包内存池**预分配**数量，推荐保持 2 的幂次。如果是在网关节点，可以加大到 1024/2048 来减少运行时的分配导致抖动。 |
| `kPacketBufferSize` | `1500` | 内存池发包 Buffer 大小，严格贴合典型以太网 MTU。千万不要改大，会引发 IP 层报文分片导致性能暴跌。 |
| **握手与协议** | |
| `kHandshakeTimeoutMs` | `5000` (5秒) | TLS 握手最大容忍耗时，防慢速攻击。 |
| `kDefaultTlsVerifyPeer` | `false` | **切记**：这里写死控制了默认不校验 TLS 对端证书的真实性（为了方便你本地直接起测试跑）。如果要推上生产环境，记得改 `true` 或者通过运行时覆写。 |

当你修改了上述头文件的配置后，请记得重新执行 `cmake --build` 重新编译底层库才会生效！
