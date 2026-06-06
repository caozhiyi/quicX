# 包的生命周期：从网卡到 frame

本文档梳理一个 UDP datagram 在 quicX 内部走过的全部路径——从 socket 可读、经过解析与路由、解密、最终拆解为 frame 派发到上层的整个过程。读完后你应当能够：

- 指出每个环节的代码位置与责任边界；
- 说出多线程模型下，**包从哪个线程开始、在哪个线程结束**；
- 在排查"包被丢了"或"包被处理两次"问题时，知道在哪一层加日志/断点。

本文只覆盖**接收方向**（receive path）。发送方向（worker 主动调度 + sendmmsg 批量发出）是另一条独立链路，参见 [`design/ownership_and_memory.md`](ownership_and_memory.md) 的发送侧讨论。

---

## 1. 总体路径

```
[ kernel UDP socket ]
        │  EPOLLIN / EVFILT_READ / FD_READ
        ▼
UdpReceiver::OnRead(fd)              ← src/quic/udp/udp_receiver.cpp
        │  RecvFromBatch（recvmmsg）    一次系统调用最多取 64 个 datagram
        │  填充 NetPacket（来自 thread-local 包池）
        ▼
IPacketReceiver::OnPacket(pkt)       ← src/quic/udp/if_receiver.h
        │  Master 是 receiver，再分发到 Worker
        ▼
MsgParser::ParsePacket()             ← src/quic/quicx/msg_parser.cpp
        │  解 header flag → 识别 long/short、版本、DCID
        │  产物：PacketParseResult（含 cid、packets 数组、原始 datagram size）
        ▼
按 DCID 路由到 Worker
   ├─ Master::OnPacket       ← src/quic/quicx/master.cpp
   │      cid_worker_map_ 查找；未命中则随机派发
   │
   └─ WorkerWithThread::HandlePacket    ← src/quic/quicx/worker_with_thread.cpp
          ThreadSafeBlockQueue::Emplace（跨线程交付）
        ▼
Worker 的事件循环 Pop 出来
   └─ Worker::HandlePacket → InnerHandlePacket
        │  ServerWorker / ClientWorker 各自实现
        │  conn_map_ 查找 → 命中走老连接；未命中且为 Initial → 新建
        ▼
BaseConnection::OnPackets(now, packets)   ← src/quic/connection/connection_base.cpp
        │  state-machine 过滤（Closing / Draining 走专用分支）
        │  for each packet: DispatchByType
        ▼
按 packet 类型分流
   ├─ OnInitialPacket   →  装 Initial keys → OnNormalPacket
   ├─ OnHandshakePacket →  OnNormalPacket
   ├─ On0rttPacket      →  OnNormalPacket（用 early-data keys）
   ├─ On1rttPacket      →  OnNormalPacket（带 Key-Phase 检测）
   ├─ OnRetryPacket     →  特例：客户端再发一次 Initial
   └─ OnVersionNegotiationPacket → 特例：客户端切版本重连
        ▼
IPacket::DecodeWithCrypto(...)       ← src/quic/packet/*.cpp
        │  Header Protection 还原 → AEAD 解密 → 拆 frames
        ▼
FrameProcessor::HandleFrames(...)    ← src/quic/connection/connection_frame_processor.cpp
        │  遍历 frames，按 FrameType 调用对应处理函数
        ▼
派发到对应 manager
   ├─ STREAM / RESET_STREAM / STOP_SENDING / MAX_STREAM_DATA → StreamManager
   ├─ ACK                                                    → SendManager
   ├─ CRYPTO                                                 → ConnectionCrypto / TLS
   ├─ NEW_CONNECTION_ID / RETIRE_CONNECTION_ID               → ConnectionIDCoordinator
   ├─ PATH_CHALLENGE / PATH_RESPONSE                         → PathManager
   └─ CONNECTION_CLOSE / CONNECTION_CLOSE_APP                → StateMachine + ConnectionCloser
        ▼
   RecvControl::OnPacketRecv（记录 packet number、决定何时回 ACK）
        ▼
   reset idle timeout
```

---

## 2. 阶段一：socket 可读 → NetPacket

### 2.1 谁在监听 fd

EventLoop 在某个线程上跑，UDP socket 的 fd 通过 [`UdpReceiver::AddReceiver`](../../src/quic/udp/udp_receiver.cpp) 注册到 EventLoop 的 IO multiplexer。Linux 用 epoll、macOS 用 kqueue、Windows 用 select/IOCP，统一封装在 [`src/common/network/if_event_driver.h`](../../src/common/network/if_event_driver.h)。

注册时挂的 handler 是 `UdpReceiver` 自身（它实现了 `IFdHandler` 接口），所以可读事件触发时由 `UdpReceiver::OnRead(fd)` 处理。

### 2.2 一次 OnRead 抽干 socket

历史上 `OnRead` 每次 `recvfrom` 单包，导致每个包一次完整 wakeup → 处理 → 回 ACK 的循环，ACK 聚合（`kAckThreshold=10`）几乎从不触发，吞吐被严重压低。

当前实现走批量路径：

| 步骤 | 行为 |
| :--- | :--- |
| 1 | 从 thread-local 的 `IPacketAllotor::Malloc()` 拿一组 `NetPacket` |
| 2 | 校验每个 NetPacket 的 writable span 是否能容下完整 IPv4 MTU（pool 复用陷阱见 `udp_receiver.cpp` 注释 §2.4） |
| 3 | 一次 `RecvFromBatch`：Linux 单次 `recvmmsg(MSG_DONTWAIT)`；macOS/Windows 是 `recvmsg`/`WSARecvMsg` 循环 |
| 4 | 为每个收到的 datagram 设置 peer address、socket fd、接收时间戳、ECN 字节 |
| 5 | 调 `IPacketReceiver::OnPacket(pkt)` 一个接一个交付 |

### 2.3 NetPacket 是什么

定义在 [`src/quic/udp/net_packet.h`](../../src/quic/udp/net_packet.h)，本质是一个**已收到的 datagram 的载体**：

| 字段 | 含义 |
| :--- | :--- |
| `buffer_` | 字节内容（`shared_ptr<IBuffer>`，buffer 内部 chunk 来自 `BlockMemoryPool`） |
| `addr_` | 对端地址 |
| `sock_` | 收到此包的 socket fd |
| `time_` | 接收时间戳（毫秒，UTC），用于 RTT 与 idle timeout |
| `ecn_` | IP ECN codepoint（2 bit） |

NetPacket 不持有任何协议语义——它就是"网卡来的一坨字节 + 元数据"。

### 2.4 缓冲来自池

`NetPacket` 用的 `IBuffer` 由 [`IPacketAllotor`](../../src/quic/udp/if_packet_allotor.h) 分配。生产路径走 `PoolPacketAllotor`，底层 chunk 来自 `common::BlockMemoryPool`。一个常见陷阱：池回收的 NetPacket 可能因为外部还持有 `SharedBufferSpan` 引用，可写区域被压到不足 MTU。`OnRead` 用一个最多 8 次的 retry 循环规避，仍拿不到干净缓冲就缩短本轮 batch。

---

## 3. 阶段二：解析与路由

### 3.1 IPacketReceiver::OnPacket 的两种实现

| 模式 | receiver 实现 | 路由方式 |
| :--- | :--- | :--- |
| 单线程（`QuicClient` / 简单服务） | `Master` | 收下后直接进入 §4 |
| 多线程（`MasterWithThread` + 多个 `WorkerWithThread`） | `Master` | 见 §3.3 跨线程派发 |

### 3.2 MsgParser 抽取 CID

[`MsgParser::ParsePacket`](../../src/quic/quicx/msg_parser.cpp) 做两件事：

1. 调 `DecodePackets()`（[`src/quic/packet/packet_decode.cpp`](../../src/quic/packet/packet_decode.cpp)）把一个 datagram 拆成一组 `IPacket`（一个 datagram 可能包含多个 coalesced packet，例如 Initial + Handshake + 1-RTT）；
2. 取首个 packet 的 header，提出 `Destination Connection ID`，写进 `PacketParseResult.cid_`。

> `DecodePackets` 的边界处理细节（无法解码的 trailing 字节、未知版本、coalesced 后续无法解密的 packet）参见源码内的 RFC 9000 §12.2 注释。

### 3.3 跨线程派发（多线程模式）

```
Master 线程                              Worker 线程
─────────────                            ─────────────
Master::OnPacket
   │
   └→ MsgParser::ParsePacket
          │
          ├─ cid_worker_map_ 命中
          │    → worker->HandlePacket(packet_info)
          │           │
          │           └→ WorkerWithThread::HandlePacket
          │                    packet_queue_.Emplace(std::move(packet_info))   ─────┐
          │                                                                         │
          └─ 未命中（典型：Initial）                                                   │
               → 随机选一个 worker，同上                                               │
                                                                                     ▼
                                                              Worker 的 EventLoop 唤醒
                                                              Worker::Process / loop tick
                                                                  packet_queue_.TryPop
                                                                  → InnerHandlePacket
```

**两个易错点**：

1. **`PacketParseResult` 必须可移动**。它持有 `shared_ptr<NetPacket>` 和一组 `shared_ptr<IPacket>`，跨线程交付时 `Emplace(std::move(...))` 走的是移动；如果手写了不必要的 copy ctor，会"静默退化为深拷贝"，每次过队列都会泄露一份引用，最终把 NetPacket 与底层 BufferChunk 钉死。`msg_parser.h` 的注释里有过一次 92MB/132MB 的内存泄漏复现记录，结论是**遵循 Rule of Zero，不要写自己的 copy ctor**。
2. **CID 表只是 hint**。`cid_worker_map_` 不命中时的"随机选 worker"看起来很离谱，但 Initial 包本来就还没分配 CID-to-worker 映射，必须有一个 worker 接住它并新建连接；新建之后 `HandleAddConnectionId` 会把 CID 写进 master 的表，后续才会精准路由。

### 3.4 单线程模式

单线程模式**仍然有 master**，只是 master 与 worker 共用同一个 `IEventLoop`、跑在同一个线程上，因而省掉了 `WorkerWithThread` 那一层和跨线程的 `packet_queue_`：

- `MasterWithThread` 照常起来，`UdpReceiver` 注册在 master 的 event loop 上；
- worker 通过 `master_event_loop_->AddFixedProcess(worker, ...)` 挂到同一个 loop，每轮 tick 由 master 线程顺手驱动 `worker->Process()`（见 [`quic_client.cpp`](../../src/quic/quicx/quic_client.cpp) `Init()` 中 `kSingleThread` 分支）；
- 收包路径变成：`UdpReceiver::OnRead` → `Master::OnPacket` → `MsgParser::ParsePacket` → `Worker::HandlePacket` → 直接 `Worker::InnerHandlePacket`，**不入队、不切线程**。

`QuicClient` 默认就是这个模式。`if_worker.h` 上的关停契约也据此明确写过："单线程模式下 master 线程同时就是 worker 线程，对 master 做 `Stop + Join` 就足够了。"


---

## 4. 阶段三：连接侧入口

到这里包终于进入了某个具体连接的处理。`Worker::InnerHandlePacket` 是个虚方法，分两种实现：

### 4.1 ServerWorker::InnerHandlePacket

[`src/quic/quicx/worker_server.cpp`](../../src/quic/quicx/worker_server.cpp) 的核心分支：

| 情况 | 行为 |
| :--- | :--- |
| `conn_map_` 命中 DCID | 取 connection，更新 socket / peer address / ECN，调 `OnPackets` |
| 未命中，且首包不是 Initial | 丢弃（错误日志） |
| 未命中，首包是 Initial，datagram < 1200 字节 | 拒绝（RFC 9000 §14.1） |
| 未命中，版本不支持 | 发 Version Negotiation |
| 未命中，需要 Retry | 发 Retry 包，**不**新建连接 |
| 未命中，可以建连 | `make_shared<ServerConnection>` 注册 callbacks，注入 sender，绑定 transport params，加入 `connecting_set_`，启动握手 watchdog timer，最后 `OnPackets` |

> Retry 策略由 `RetryPolicy::{NEVER, ALWAYS, SELECTIVE}` 控制。SELECTIVE 模式下读 `ConnectionRateMonitor` 与 `IPRateLimiter` 的状态决定是否发 Retry。

### 4.2 ClientWorker::InnerHandlePacket

客户端的 `conn_map_` 通常只有一条记录（典型场景），实现比 server 简单得多：直接 `conn_map_.find` → `OnPackets`。Version Negotiation / Retry 的处理逻辑落在 `ClientConnection::On*Packet` 内部而不是 worker 层。

### 4.3 一个 pin 的细节

`InnerHandlePacket` 在调 `OnPackets` 之前，会用一个**局部 `shared_ptr` copy** 钉住 connection：

```cpp
auto connection = conn->second;   // 而不是 conn->second.get()
...
connection->OnPackets(...);
```

原因：`OnPackets` 内部可能触发 `OnStateToDraining` → `InvokeConnectionCloseCallback`，后者会从 `conn_map_` 移除 entry。如果不 pin，迭代器持有的就是最后一个引用，`this` 会在调用进行中被析构。

---

## 5. 阶段四：状态机门禁

[`BaseConnection::OnPackets`](../../src/quic/connection/connection_base.cpp) 上来先看连接状态：

| 状态 | 处理 |
| :--- | :--- |
| `Closing` | 走 `HandlePacketsInClosingState`：尝试解密找 `CONNECTION_CLOSE`；找到则进入 Draining；找不到则按节奏重传我们之前发的 CLOSE |
| `Draining` / `Closed` | 走 `DropPacketsInDrainingState`：全部丢弃（如果开了 qlog，每个被丢的包记一条 `packet_dropped`） |
| `Connecting` / `Connected` | 进入正常处理 |

正常处理对每个 `IPacket` 调 `DispatchByType`，按 packet 类型路由到对应 `On*Packet` 方法（见 §1 总览）。处理成功后调 `RecvControl::OnPacketRecv` 登记 packet number，作为后续 ACK 决策的输入。

最后 `timer_coordinator_->ResetIdleTimer()`——**只要这个 datagram 包含一个能解密成功的 packet，idle timer 就重置**。这是 RFC 9000 §10.1 的要求。

---

## 6. 阶段五：解密与帧拆解

### 6.1 OnNormalPacket 的统一入口

无论 Initial / Handshake / 0-RTT / 1-RTT，最终都汇入 `OnNormalPacket`。它做三件事：

1. 从 `ConnectionCrypto` 取出对应 encryption level 的 `ICryptographer`；
2. 调 `IPacket::DecodeWithCrypto(buffer)`：去 Header Protection、AEAD 解密、把 payload 拆成 frame 序列；
3. 把 frames 交给 `FrameProcessor`。

每种 packet 类型对应不同的 `IPacket` 子类（[`src/quic/packet/`](../../src/quic/packet/)），它们各自实现自己的 `DecodeWithCrypto`：

| 类型 | 文件 |
| :--- | :--- |
| Initial | `init_packet.cpp` |
| Handshake | `handshake_packet.cpp` |
| 0-RTT | `rtt_0_packet.cpp` |
| 1-RTT | `rtt_1_packet.cpp` |
| Retry | `retry_packet.cpp` |
| Version Negotiation | `version_negotiation_packet.cpp` |

### 6.2 1-RTT 的 Key Update 旁路

`On1rttPacket` 在解密失败时会检查 Key Phase 是否翻转（`IsKeyPhaseChanged` + `CanKeyUpdate`），如果是，触发 `TriggerReadKeyUpdate` 然后 `RetryPayloadDecrypt`。这是 RFC 9001 §6 被动 Key Update 的实现路径。

### 6.3 FrameProcessor::HandleFrames

[`src/quic/connection/connection_frame_processor.cpp`](../../src/quic/connection/connection_frame_processor.cpp)。一个 `switch (frame->GetType())` 把每种 frame 派发到对应模块。常见映射：

| Frame | 接收方 |
| :--- | :--- |
| `STREAM`, `RESET_STREAM`, `STOP_SENDING`, `MAX_STREAM_DATA` | `StreamManager` |
| `MAX_DATA`, `MAX_STREAMS_*`, `DATA_BLOCKED`, `STREAMS_BLOCKED` | `BaseConnection` 自身的 flow controller |
| `ACK` | `SendManager::OnPacketAck` |
| `CRYPTO` | `ConnectionCrypto` → 喂给 BoringSSL |
| `NEW_CONNECTION_ID`, `RETIRE_CONNECTION_ID` | `ConnectionIDCoordinator` |
| `PATH_CHALLENGE`, `PATH_RESPONSE` | `PathManager` |
| `NEW_TOKEN` | `SessionCache`（客户端） |
| `CONNECTION_CLOSE`, `CONNECTION_CLOSE_APP` | `state_machine_` + `connection_closer_` |
| `PING`, `PADDING` | 仅作为"ack-eliciting / non-ack-eliciting" 计入 `RecvControl` |
| `HANDSHAKE_DONE` | `state_machine_.OnHandshakeDoneFrameReceived`（仅客户端） |

每个分支都可能更新 `RecvControl` 的"是否需要立即回 ACK"判断，并可能把当前连接加入 worker 的 `active_send_connections_` 让下一次 `ProcessSend` 处理回包。

---

## 7. 关键不变量

整理几条贯穿全链路的不变量，便于排查：

1. **NetPacket 的 buffer 在 `OnPackets` 返回后才允许释放**。`packets_` 数组里每个 `IPacket` 持有指向 buffer 的 `SharedBufferSpan`，提前释放会导致解密读到回收后的内存。
2. **CID 路由表（master 的 `cid_worker_map_` + worker 的 `conn_map_`）只在自己的 EventLoop 线程上修改**。新增 CID 走 `HandleAddConnectionId` 回调；销毁走 `HandleRetireConnectionId` 与 `HandleConnectionClose`。
3. **packet 必须先 `DecodeWithCrypto` 成功，才 `OnPacketRecv`**。失败的包不进入 ACK tracker，避免被错误 ACK。
4. **idle timer 在每个解密成功的 datagram 后重置**，一个 datagram 内多个 coalesced packet 重置一次即可（实现层面在 `OnPackets` 末尾统一做）。
5. **`HandlePacketsInClosingState` 不会为接收的 packet 触发 ACK**——一旦进入 Closing，本端只关心是否对端也发了 CLOSE，从而进入 Draining。

---

## 8. 关联文档

- [`design/ownership_and_memory.md`](ownership_and_memory.md) —— 解释为什么 `BaseConnection` / `IStream` 用 `weak_ptr<IEventLoop>` 等所有权约定。
- [`design/handshake_state_machine.md`](handshake_state_machine.md) —— 从 Initial 包接收到握手完成的状态转移。
- [`design/loss_recovery.md`](loss_recovery.md) —— ACK 处理之后如何驱动重传（RFC 9002）。
- [`tutorial/quic_api_guide.md`](../tutorial/quic_api_guide.md) —— 站在用户视角的接收数据 API。
- [`reference/qlog_event_coverage.md`](../reference/qlog_event_coverage.md) —— `packet_received` / `packet_dropped` 等事件的字段覆盖。

---

## 9. 关联 RFC

- [RFC 9000 §12.2](https://www.rfc-editor.org/rfc/rfc9000.html#name-coalescing-packets) —— Coalescing Packets
- [RFC 9000 §14.1](https://www.rfc-editor.org/rfc/rfc9000.html#name-initial-datagram-size) —— Initial Datagram Size
- [RFC 9000 §17.2.1](https://www.rfc-editor.org/rfc/rfc9000.html#name-version-negotiation-packet) —— Version Negotiation Packet
- [RFC 9000 §10.1](https://www.rfc-editor.org/rfc/rfc9000.html#name-idle-timeout) —— Idle Timeout
- [RFC 9001 §5.4](https://www.rfc-editor.org/rfc/rfc9001.html#name-header-protection) —— Header Protection
- [RFC 9001 §6](https://www.rfc-editor.org/rfc/rfc9001.html#name-key-update) —— Key Update
- [RFC 9369](https://www.rfc-editor.org/rfc/rfc9369.html) —— QUIC Version 2（v1/v2 wire 类型映射在 `packet_decode.cpp` 内处理）
