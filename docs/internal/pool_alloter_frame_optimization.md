# PoolAlloter 接入 Frame 分配 —— 调研与可行性评估

> 状态：**已调研，暂缓实施（非当前优先级）**
> 调研日期：2026-05
> 适用范围：`src/quic/frame/`、`src/quic/stream/`、`src/quic/connection/`、`src/quic/packet/`
> 相关代码：`src/common/alloter/pool_alloter.h`、`src/common/alloter/if_alloter.h`

---

## 0. TL;DR

- **目标**：用 `PoolAlloter` 替代热路径上的 `std::make_shared<XxxFrame>`，降低分配开销与 `shared_ptr` 控制块成本。
- **结论**：技术上可行，但**全链路改造代价高、收益边际递减**，且与当下「互操作 / 稳定性」优先级冲突。
- **建议**：暂缓全量改造；若未来要做，按 **P0 → P1** 渐进推进，**P2 不建议做**。
- **当前不实施的核心理由**：
  1. 接收路径（packet `frames_list_` + recv/crypto stream 乱序缓存）必须改 move 语义，影响 ~25 个文件、~2000 行级别。
  2. 23 处 `dynamic_pointer_cast` 需联动改造，悬空 / double-move 风险明显。
  3. 项目正在跑互操作测试，大面积接口签名变动 ROI 低、退化风险高。
  4. 实测最热点的发送侧 frame 生命周期已可被「P0 局部改造」覆盖，无需牵动接收侧。

---

## 1. 背景

`PoolAlloter` 是 free-list slab 分配器（≤256B、非线程安全、连接生命周期内不归还系统）。
QUIC 热路径上每次 `TrySend` / `OnPacket` 会产出多个 frame，目前全部走 `std::make_shared<XxxFrame>`：

- 控制块（16~24B）+ 计数 atomic inc/dec（ns 级）。
- 高 PPS 场景下叠加可观察到。

调研目标：**在不破坏现有所有权语义、不影响互操作的前提下**，把 frame 分配迁到 PoolAlloter，并评估是否值得做。

---

## 2. 关键事实复盘（Frame 生命周期真相）

通过全路径源码核查得到几个**与最初假设相反**的关键结论：

### 2.1 ✅ 发送路径的 packet **不持有 frame 对象**

```cpp
// src/quic/connection/packet_builder.cpp
packet->SetPayload(ctx.frame_visitor->GetBuffer()->GetSharedReadableSpan());
```

`BuildPacket` / `BuildDataPacket` 只把 frame visitor 编码后的**字节 buffer** 塞给 `packet->SetPayload()`。
packet 的 `frames_list_` 在发送路径**始终为空**——它只在 `DecodeWithCrypto` 内由 `DecodeFrames(..., frames_list_)` 填充。

```cpp
// src/quic/packet/if_packet.cpp:6-9
std::vector<std::shared_ptr<IFrame>>& IPacket::GetFrames() {
    static std::vector<std::shared_ptr<IFrame>> s_no_use;
    return s_no_use;
}
```

### 2.2 ✅ 重传**不需要 frame 对象**，直接复用已加密 payload 字节

```cpp
// src/quic/packet/rtt_1_packet.cpp:23-70
bool Rtt1Packet::Encode(std::shared_ptr<common::IBuffer> buffer) {
    ...
    auto payload_span = payload_.GetSpan();    // 直接用 payload_ 字节
    auto result = crypto_grapher_->EncryptPacket(packet_number_, ad_span, payload_span, buffer);
}
```

```cpp
// src/quic/connection/connection_base.cpp:1624-1631（重传路径）
auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);
if (!lost_pkt->Encode(buffer)) { ... }    // 仅依赖 payload_ + 新 PN + 新密钥
```

**lost_pkt 只需要 `payload_`（编码后字节），不需要 `frames_list_`。**

### 2.3 ✅ `PacketTimerInfo.packet` 持有的是 packet 字节，不是 frame

```cpp
// src/quic/connection/controler/send_control.cpp:110-111
unacked_packets_[ns][packet->GetPacketNumber()] =
    PacketTimerInfo(largest_sent_time_[ns], pkt_len, timer_task, stream_data, packet);
```

ACK 触发的 `stream_data_ack_cb_(stream_id, max_offset, has_fin)` 用的是 `StreamDataInfo` 值类型，**完全不触达 frame 对象**。

### 2.4 ✅ `wait_frame_list_` 跨调用保留，但时间窗口很短

```cpp
// src/quic/connection/controler/send_manager.cpp:297-328
for (auto iter = wait_frame_list_.begin(); iter != wait_frame_list_.end();) {
    ...
    iter = wait_frame_list_.erase(iter);
}
```

frame 在 `ToSendFrame` → `GetPendingFrames` 之间停留，通常同一事件循环迭代内即被消费。

### 2.5 ⚠️ 接收路径 packet **真正持有 frame**

```cpp
// src/quic/packet/rtt_1_packet.cpp:212-220
if (!DecodeFrames(plaintext_buffer, frames_list_)) { ... }
for (const auto& frame : frames_list_) {
    frame_type_bit_ |= (1u << frame->GetType());
}
```

```cpp
// src/quic/connection/connection_base.cpp:765-926
if (!OnFrames(packet->GetFrames(), packet->GetCryptoLevel())) { ... }
```

加上：

- `RecvStream::out_order_frame_`（`unordered_map<uint64_t, shared_ptr<IFrame>>`）
- `CryptoStream::out_order_frame_[level]`

这些缓存会**长期持有** out-of-order frame 直到拼接完成，是接收侧最重的所有权链。

### 2.6 ✅ 跨线程边界不传 frame

`WorkerWithThread::HandlePacket` 通过 `packet_queue_` 跨线程传递 `PacketParseResult`（**只含编码字节，frame 未 decode**），frame decode 发生在目标线程的 `worker_ptr_->HandlePacket`。
👉 frame 在单个 worker 线程内生命周期完整，**无跨线程所有权问题**。

---

## 3. 全链路改造工作量评估

### 3.1 要改动的接口与数据结构

| 层 | 文件 | 改动 | 难度 |
|---|---|---|---|
| frame 工厂 | `frame_decode.cpp/.h` | `kFrameCreatorMap` 的 `function<shared_ptr<IFrame>(uint16_t)>` → `function<PoolUniquePtr<IFrame>(uint16_t, IAlloter*)>`；`DecodeFrames` 签名改 | 🟡 中 |
| visitor 接口 | `if_frame_visitor.h`、`fix_buffer_frame_visitor.*` | `HandleFrame(shared_ptr<IFrame>)` → `HandleFrame(IFrame*)`；2 处 `dynamic_pointer_cast` → `dynamic_cast` | 🟢 易 |
| packet 接口 | `if_packet.h/cpp` + 4 个 packet 实现 | `vector<shared_ptr<IFrame>>` → `vector<PoolUniquePtr<IFrame>>` | 🟡 中 |
| frame 处理入口 | `connection_frame_processor.*` | `OnFrames` 签名改；**13 处 `dynamic_pointer_cast<XFrame>(frame)` → `dynamic_cast<XFrame*>(frame.get())`** | 🟡 中 |
| recv/crypto stream 乱序缓存 | `recv_stream.*`、`crypto_stream.*` | `unordered_map` 的 value 类型改、insert/find/extract 全部走 move | 🔴 难 |
| send 侧 frame 生产 | `send_stream.cpp`、`crypto_stream.cpp`、`connection_base.cpp` | 20+ 处 `make_shared<XFrame>` → `PoolMakeUnique<XFrame>(pool)` | 🟢 易 |
| send_manager 队列 | `send_manager.*`、`if_stream.h` | `list<shared_ptr<IFrame>> wait_frame_list_` → `list<PoolUniquePtr<IFrame>>`；`ToSendFrame` / `OnFrameReady` 签名改 move | 🔴 难 |
| AckFrame in SendControl | `send_control.cpp` | `OnPacketAck(ns, shared_ptr<IFrame>)` 改；2 处 `dynamic_pointer_cast` 改 | 🟢 易 |

### 3.2 `dynamic_pointer_cast` → `dynamic_cast` 改造点（共 23 处）

- `fix_buffer_frame_visitor.cpp` × 2（`StreamFrame`、`CryptoFrame`）
- `connection_frame_processor.cpp` × 13（`AckFrame`、`AckEcnFrame`、`MaxStreamDataFrame`、`StopSendingFrame`、`NewTokenFrame`、`MaxDataFrame`、`MaxStreamsFrame`、`NewConnectionIDFrame`、`RetireConnectionIDFrame`、`ConnectionCloseFrame`、`PathChallengeFrame`、`PathResponseFrame`、`IStreamFrame`）
- `send_control.cpp` × 2（`AckFrame`、`AckEcnFrame`）
- `send_stream.cpp` / `recv_stream.cpp` / `crypto_stream.cpp` × ~6（`StreamFrame`、`CryptoFrame`、`ResetStreamFrame`、`StreamDataBlockedFrame` 等）

**全部可机械替换**，无语义风险，但变动面广。

### 3.3 核心难点：乱序缓存的 unique_ptr 化

```cpp
// recv_stream.h:54
std::unordered_map<uint64_t, std::shared_ptr<IFrame>> out_order_frame_;
```

改为：

```cpp
std::unordered_map<uint64_t, PoolUniquePtr<IFrame>> out_order_frame_;
```

赋值语义被迫从拷贝改为 move：

```cpp
out_order_frame_[offset] = std::move(stream_frame);
```

而 `stream_frame` 来源于 `DecodeFrames` 产出的 vector——这就要求 `OnFrames` 接口能让 processor 把 frame **从 vector 中 move 出来**，所有权语义牵动整条接收链。

---

## 4. 可行性评级

🎯 **结论：技术可行，但代价高 / 收益稀释。当前阶段不推荐做完整改造。**

### 理由
1. ✅ **生命周期障碍已消除**：实测确认发送侧 frame 真实生命周期 == 一次 `TrySend` 迭代，`PoolUniquePtr` 在发送侧本身可用。
2. ⚠️ **接收路径是主要阻力**：`frames_list_` + 乱序缓存让接收侧必须改 move 语义，影响范围大。
3. ⚠️ **收益稀释**：QUIC 中分配量最大的是接收路径，但接收 frame 最终要进 stream 缓存重组 —— 无论 unique_ptr 还是 shared_ptr 都得走 pool；节省的本质只是 shared_ptr 控制块（16~24B）+ atomic inc/dec（ns 级）。在 ACK 密集 / 小包场景有收益，大流量场景下占比有限。
4. ⚠️ **风险**：23 处 `dynamic_cast` 改造 + move 语义严格化，容易引入悬空指针 / double-move bug，**对一个跑互操作测试的栈而言退化风险大**。

---

## 5. 推荐分级实施策略（若未来重启）

| 优先级 | 范围 | 预期收益 | 改动文件 | 风险 |
|---|---|---|---|---|
| **P0（强烈推荐起点）** | 仅改发送侧临时 frame：`SendStream::TrySendData` 里 `StreamFrame` / `StreamDataBlockedFrame` / `ResetStreamFrame` 用 `PoolMakeUnique`（per-connection pool）；visitor 增加 `IFrame*` 接收重载；`wait_frame_list_` 暂保 `shared_ptr` 不动 | 命中最热路径（每次 write 一次），约 **70% 收益** | 4~5 文件：`send_stream.cpp`、`if_frame_visitor.h`、`fix_buffer_frame_visitor.cpp`、`connection_base.cpp`（pool 注入）、`base_connection.h` | 低 |
| **P1（可选）** | 把 `wait_frame_list_` 改 `list<PoolUniquePtr>`；`ToSendFrame` / `GetPendingFrames` 改 move 语义；CRYPTO/ACK/Flow frame 也走 pool | 再 +15% 收益 | 3~4 文件 | 中 |
| **P2（不推荐）** | 接收路径 + packet `frames_list_` + recv/crypto stream 乱序缓存全部 unique_ptr 化 | 边际 +15% | 15+ 文件、~2000 行 | 高 |

### P0 落地要点（备忘）
1. `BaseConnection` 新增 `pool_`（per-connection `PoolAlloter`）。
2. `IFrameVisitor` 增加 `IAlloter* GetAlloter()` 访问器。
3. `FixBufferFrameVisitor` 新增 `HandleFrame(IFrame*)` 重载（不接管所有权，只读 frame 内容）；保留旧 `shared_ptr` 重载兼容。
4. `SendStream::TrySendData` 把 `make_shared<StreamFrame>()` 改成 `PoolMakeUnique<StreamFrame>(visitor->GetAlloter())`，调用新重载，frame 在 `TrySendData` 退栈时被 PoolDeleter 自动归还。

P0 的最大优势：**不动接收侧、不动 packet 接口、不改 `wait_frame_list_`、不动 `dynamic_pointer_cast`**，闭环 4~5 个文件。

---

## 6. 暂缓决策的依据

1. 当前重点：**互操作测试 / 稳定性 / 正确性**。frame 分配开销不在主要瓶颈榜上。
2. 已有更高收益的替代项，如 `ThreadLocalBlockPool` 已覆盖 1500B BufferChunk 池（收发数据缓冲区，体量更大）。
3. P0 收益虽然存在，但需要先做 perf 数据基线对比（看 `make_shared` 在 perf 火焰图中的真实占比，参考 `docs/internal/perf_flamegraph_analysis.md`），否则盲改属于「无依据微优化」。
4. 大改造（P1/P2）必须搭配端到端基准 + 长稳测试 + 互操作回归，工程预算不在本季度。

---

## 7. 重启该工作的触发条件（建议）

在以下任一条件成立时再考虑重启：

- perf 火焰图中 `operator new` / `_M_construct` / `make_shared` 在 frame 分配相关栈上占比 >5%。
- 高 PPS 服务端场景（>50K conn）出现内存抖动 / TLB miss 增加，怀疑与小对象碎片相关。
- 已有完整的端到端 perf benchmark 基线（参考 `test/perf/`），可量化前后差异。
- 互操作矩阵稳定通过，可承受较大面积的接口变更。

满足上述条件后，**仅推进 P0**，并以基准数据决定是否继续 P1。

---

## 8. 已完成的前置工作（保留备用）

- `src/common/alloter/if_alloter.h` 已新增：
  - `PoolDeleter<T>`：无状态 deleter，仅持 `IAlloter*`。
  - `PoolUniquePtr<T>`：`unique_ptr<T, PoolDeleter<T>>`，sizeof = 16B。
  - `PoolMakeUnique<T>(IAlloter*, Args&&...)`：构造工具。
  - 旧 `PoolNewSharePtr` 已加 `[[deprecated]]`。

这些设施可在未来 P0 启动时直接使用，无需再造。

---

## 9. 参考

- `src/common/alloter/pool_alloter.h`、`src/common/alloter/if_alloter.h`
- `src/quic/frame/frame_decode.cpp`
- `src/quic/stream/fix_buffer_frame_visitor.cpp`
- `src/quic/connection/connection_frame_processor.cpp`
- `src/quic/connection/controler/send_control.cpp`、`send_manager.cpp`
- `src/quic/connection/connection_base.cpp`（重传路径 L1595-1643、发送路径 L1728-1729）
- `src/quic/packet/if_packet.cpp`、`rtt_1_packet.cpp`
- `docs/internal/perf_flamegraph_analysis.md`（建议结合 perf 数据再决策）
