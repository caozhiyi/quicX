# 丢包检测与恢复

本文档梳理 quicX 中 RFC 9002 *Loss Detection and Congestion Control* 的实现：一个已发出的包从"登记进 unacked 表"，到被 ACK 抹掉、被时间/包阈值判丢、或被 PTO 探测兜底的全部路径。读完后你应当能够：

- 知道**哪个类**负责 RTT 估算、判丢、PTO，以及它们如何串成一条数据流；
- 在排查"重传没发出来"或"PTO 不停打转"问题时，准确定位到代码片段；
- 区分 quicX 里两种"包级定时器"——per-packet `timer_task_` 与全局 `pto_timer_`——分别在解决什么问题。

本文只覆盖**发送方向**的可靠性逻辑（loss detection + retransmission + PTO probe）。接收侧的 ACK 触发策略（阈值 / OoO / gap / max_ack_delay）见 [`packet_lifecycle.md`](packet_lifecycle.md) §6.3 与 `recv_control.cpp::ShouldSendImmediateAck`；拥塞控制算法本身（Reno / Cubic / BBR）是另一条独立链路，不在本文展开。

---

## 1. 总体路径

下面这张图把一个 ACK-eliciting 包从"发出"到"被 ACK / 被判丢 / 被 PTO 兜底 / 被重传"的四条链路画在同一张图里，每个节点都标注**实际函数名**，方便和源码对照：

```text
═══════════════════════════════════════════ ① 发送登记 ═══════════════════════════════════════════

  BaseConnection::TrySendNew                        组装 frames + ACK，构建 IPacket
        │
        ▼
  BaseConnection::SendBuffer ──► sendmmsg
        │
        ▼
  SendControl::OnPacketSend(packet, len, stream_data)              ◄── 关键登记入口
        │
        ├── 非 ACK-eliciting ── 仅更新 largest_sent；不入 unacked，不计 cwnd
        │
        └── ACK-eliciting
              ├── congestion_control_->OnPacketSent       bytes_in_flight += len
              ├── unacked_packets_[ns][pn] = PacketTimerInfo{ send_time, len, task, ... }
              ├── per-packet timer_task_                  PTO 后兜底标 lost
              └── 重置全局 pto_timer_                     = PTO_with_backoff()


═══════════════════════════════════════════ ② 反馈路径 ═══════════════════════════════════════════

  对端 ACK 到达
        │
        ▼
  FrameProcessor::HandleFrames ──► SendManager::OnPacketAck ──► SendControl::OnPacketAck
        │
        ├── rtt_calculator_.UpdateRtt(send_time, now, ack_delay)              【§2 RTT】
        │
        ├── 遍历 ACK ranges，对每个被 ACK 的 pn：
        │     ├── timer_->RemoveTimer(per-packet timer_task_)
        │     ├── congestion_control_->OnPacketAcked
        │     ├── stream_data_ack_cb_(stream_id, offset, len, fin)            通知 SendStream
        │     └── unacked_packets_[ns].erase(pn)
        │
        ├── DetectLostPackets(now, ns, largest_acked)                         【§3 判丢】
        │     for each unacked pn < largest_acked:
        │         if (largest_acked >= pn + 3)            → packet threshold
        │         else if (time_since_sent > 9/8·SRTT)    → time threshold
        │         → 移到 lost_packets_，调 packet_lost_cb_
        │
        ├── rtt_calculator_.OnPacketAcked()                                   重置 PTO 退避
        │
        └── 重新调度 pto_timer_                                               【§4 PTO 调度】
              ├── 有 ack-eliciting in-flight   → 重置 PTO（按当前退避）
              ├── 否则若 !handshake_complete_  → 仍调度（握手期 probe）
              └── 否则                          → 保持 cancelled


═══════════════════════════════════════════ ③ 兜底路径 ═══════════════════════════════════════════

  pto_timer_ 到期
        │
        ▼
  SendControl::OnPTOTimer                                                     【§4 PTO fire】
        │
        ├── rtt_calculator_.OnPTOExpired()                pto_count_++
        ├── 选最早的 unacked，标 lost ─► packet_lost_cb_ ─► ActiveSend
        ├── 握手期且无可重传：probe_needed_cb_            → 注入 PING (Initial/Handshake)
        ├── 握手后 always   ：application_probe_cb_      → 注入 PING (1-RTT)
        └── 重新调度 pto_timer_                           = PTO × 2^pto_count_


═══════════════════════════════════════════ ④ 重传执行 ═══════════════════════════════════════════

  ActiveSend ──► worker ──► BaseConnection::TrySend
        │
        ├── NeedReSend()  → TrySendRetransmit                                 【§5 重传】
        │     ├── 取 lost_packets_.front()
        │     ├── 分配新 PN + 当前 KeyPhase
        │     ├── 重新编码 + AEAD 加密
        │     ├── OnPacketSend(new_pn, old_stream_data)   ◄── 回到 ① 登记环节
        │     └── SendBuffer
        │
        └── 否则 TrySendNew
```

---

## 2. RTT 估算：`RttCalculator`

实现：[`src/quic/connection/controler/rtt_calculator.h`](../../../src/quic/connection/controler/rtt_calculator.h) / [`rtt_calculator.cpp`](../../../src/quic/connection/controler/rtt_calculator.cpp)（约 130 行，逻辑与 RFC 9002 §5 一一对应）。

### 2.1 三个估计量

| 字段 | 含义 | 更新公式（RFC 9002 §5） |
| :--- | :--- | :--- |
| `latest_rtt_` | 最近一次样本（now − send_time） | 直接赋值 |
| `min_rtt_` | 整连接历史最小 RTT | `min(min_rtt_, latest_rtt_)` |
| `smoothed_rtt_` | 平滑 RTT，用于 PTO / 时间阈值 | `smoothed_rtt = 7/8·smoothed_rtt + 1/8·adjusted_rtt` |
| `rtt_var_` | RTT 方差 | `rttvar = 3/4·rttvar + 1/4·\|smoothed_rtt − adjusted_rtt\|` |

`adjusted_rtt = latest_rtt − ack_delay`，且**仅在** `latest_rtt ≥ min_rtt + ack_delay` 时才扣 ACK Delay（避免减成负数把样本污染）。代码就在 `UpdateRtt()` 里：

```cpp
uint32_t adjusted_rtt = latest_rtt_;
if (latest_rtt_ >= (min_rtt_ + ack_delay)) {
    adjusted_rtt -= ack_delay;
}
```

> 已知差异：RFC 9002 §5.3 要求 handshake_confirmed 之前忽略 peer 的 `max_ack_delay`，handshake_confirmed 之后取 `min(ack_delay, peer_max_ack_delay)`。当前代码尚未引入 handshake_confirmed 信号（路线图 §2 已登记），但 `SendControl::GetEffectiveMaxAckDelay()` 在 PTO 计算路径上已经做了同等保护——握手前一律按 0 处理（见 §4.1）。

### 2.2 初始 RTT 与 P3 旋钮

在收到第一条 ACK 之前 `smoothed_rtt_` 必须有一个初值，否则 PTO 算出来是 0。RFC 9002 §6.2.2 推荐 333 ms；quicX 用 250 ms（`kInitRttDefaultMs`），让冷启动 PTO ≈ 250 + 4·125 + 25 ≈ 775 ms，与 RFC 基线在同一量级，但稍激进，对典型互联网 RTT 更友好。

为了让回环路径上的基准测试不被 ~775 ms 冷启动 PTO 拖死，引入了进程级覆盖：

| 接口 | 行为 |
| :--- | :--- |
| `GetDefaultInitialRtt()` | `RttCalculator::Reset()` 在构造 / 重置时调用，返回当前覆盖值 |
| `SetDefaultInitialRtt(ms)` | 进程级调小（仅供基准/单测）；传 0 或大于 250 都会回落到默认 |

这是 `docs/internal/perf_e2e_analysis.md` §6 的 P3 旋钮。**不要**在生产代码里调小——回环上 50 ms 的初始 PTO，到了跨大陆链路上就会在握手第二跳触发虚假重传。

### 2.3 PTO 公式与退避

```cpp
// rtt_calculator.cpp:94
uint32_t GetPT0Interval(uint32_t max_ack_delay) {
    // PTO = SRTT + max(4·RTTVAR, kGranularity) + max_ack_delay
    return smoothed_rtt_ + std::max<uint32_t>(rtt_var_ << 2, 1) + max_ack_delay;
}

uint32_t GetPTOWithBackoff(uint32_t max_ack_delay) {
    // PTO * 2^pto_count_，pto_count_ 上限 kMaxPTOBackoff = 6（即 64×）
    return GetPT0Interval(max_ack_delay) << std::min(pto_count_, kMaxPTOBackoff);
}
```

退避状态由两个调用点驱动：

| 事件 | 影响 |
| :--- | :--- |
| `OnPTOExpired()`（PTO 到期一次） | `pto_count_++`（封顶 6），`consecutive_pto_count_++` |
| `OnPacketAcked()`（任何 ACK） | 二者全部归零 |

`consecutive_pto_count_` 是给 connection idle 关连接用的——`kMaxConsecutivePTOs = 16`，约 3 个 PTO 周期，便于把"路径已死"和"网络抖动"区分开。

---

## 3. 判丢：`SendControl::DetectLostPackets`

实现：[`src/quic/connection/controler/send_control.cpp:556`](../../../src/quic/connection/controler/send_control.cpp)。每收到一个 ACK frame，`OnPacketAck` 在处理完所有 range 后调用一次，把同一 packet number space 内 `pn < largest_acked` 的未确认包扫一遍，按 RFC 9002 §6.1 两把尺子检查：

### 3.1 包阈值（RFC 9002 §6.1.1）

```cpp
// send_control.h
static constexpr uint32_t kPacketThreshold = 3;
// send_control.cpp:581
if (largest_acked >= pkt_num + kPacketThreshold) {
    should_declare_lost = true;
}
```

定义了"距离 largest_acked 至少 3 个 PN 还没回来就判丢"，3 是 RFC 推荐值，也是抗乱序的下限。

### 3.2 时间阈值（RFC 9002 §6.1.2）

```cpp
// send_control.h
static constexpr uint32_t kTimeThresholdNum = 9;   // 9/8 · SRTT
static constexpr uint32_t kTimeThresholdDen = 8;
// send_control.cpp:558
uint64_t loss_delay = (rtt_calculator_.GetSmoothedRtt() * 9) / 8;
loss_delay = std::max(loss_delay, uint64_t(1));   // 至少 1 ms
```

判定基准是 `largest_acked` 自己的 send_time（不是 now）：

```cpp
uint64_t time_since_sent =
    largest_acked_send_time > info.send_time_
        ? largest_acked_send_time - info.send_time_
        : 0;
if (time_since_sent > loss_delay) {
    should_declare_lost = true;
}
```

为什么用 `largest_acked` 的发送时间而不是当前时间？因为我们要排除"包还在路上"的可能性。如果 `pn` 比 `largest_acked` 早发了超过 9/8·SRTT，而 `largest_acked` 都已经 ACK 回来了，那 `pn` 大概率不是迟到——它是真的丢了。

### 3.3 标 lost 的副作用

任一阈值满足：

1. 取消 per-packet timer_task_（避免它再开火做重复处理）；
2. 从 `unacked_packets_[ns]` 移除（防泄漏，原条目让 retransmit 路径以新 PN 重新登记）；
3. 推入 `lost_packets_` 队列，连同 `stream_data` 一起携带；
4. `congestion_control_->OnPacketLost(...)` 通知 CC 层降窗（具体动作由 Reno/Cubic/BBR 各自决定）；
5. `packet_lost_cb_(packet)`：连接层 `SendManager` 拿这个回调统一调 `send_retry_cb_` → `BaseConnection::ActiveSend`，确保 worker 下一轮进入 send loop。

> **关键不变量**：被标 lost 的包**不会**在原 PN 上被"重发"。QUIC 不允许 PN 复用（RFC 9000 §13.2.3）。重发是用**新 PN + 同载荷**重新构造一个 packet——见 §5。

### 3.4 per-packet 兜底定时器

注意 `OnPacketSend` 还为**每个**ack-eliciting packet 单独挂了一个 `timer_task_`，PTO 时长后到期再独立判一次 lost：

```cpp
// send_control.cpp:102
auto timer_task = common::TimerTask([this, pkt_len, packet, ns] {
    auto it = unacked_packets_[ns].find(packet->GetPacketNumber());
    if (it == unacked_packets_[ns].end() || it->second.is_lost) {
        return;  // DetectLostPackets 已经处理过
    }
    it->second.is_lost = true;
    lost_packets_.push_back(LostPacketEntry{packet, it->second.stream_data});
    ...
});
```

它和 `DetectLostPackets` 是**冗余兜底**关系：

- ACK 到来时 `DetectLostPackets` 用包/时间阈值判丢——快路径；
- 长时间没有任何 ACK 触发不到上面那条路径，per-packet 定时器在 PTO 时长后自己开火——慢路径。

`is_lost` 字段是两条路径之间的去重锁：先到的标位，后到的看见就空转返回，避免 `bytes_in_flight` 双减下溢（这是 P0-1 修过一次的 bug，注释里有备注）。

---

## 4. PTO 探测：`pto_timer_` + `OnPTOTimer`

PTO（Probe Timeout）和上面的 per-packet timer 不是一个东西。它是**全局唯一**的"沉默检测器"——只要还有 ack-eliciting 在飞，就保持一根定时器；它到期不直接判丢谁，而是触发**主动探测**。

### 4.1 调度规则（RFC 9002 §6.2.1）

`pto_timer_` 在三个地方被（重新）调度：

| 调度点 | 条件 | 备注 |
| :--- | :--- | :--- |
| `OnPacketSend` 末尾 | 每发一个 ack-eliciting 包都重置 | 用 `GetPTOWithBackoff(GetEffectiveMaxAckDelay())` |
| `OnPacketAck` 末尾 | 仍有 ack-eliciting in-flight：重置；否则握手期：保留；否则：cancel | 见下方 Bug #18 注释 |
| `OnPTOTimer` 末尾 | 不论这次有没有 retransmit，都按新退避值重排 | `pto_count_` 已被 `OnPTOExpired()` 自增 |

`GetEffectiveMaxAckDelay()` 是 RFC 9002 §6.2.1 强制要求的：握手未确认前必须把 peer 的 max_ack_delay 当 0，否则 PTO 会被 peer 还没 reliably 收到的 transport param 抬高，握手期 spurious 阻塞。

### 4.2 Bug #18：分支不对的代价

`OnPacketAck` 收尾的 PTO 重新调度有过一次教训。原来的写法是"只在握手期重置 PTO"，结果握手后如果 ACK 部分覆盖了在飞包（典型场景：selective ACK 留了一个老的重传没确认），PTO 被永久取消，后续既等不到 ACK 也没新包发出（FC 卡住），连接活活坐到 idle timeout。

修复后的判定收敛为：

```cpp
// send_control.cpp:443
bool has_ack_eliciting_in_flight = false;
for (int s = 0; s < PacketNumberSpace::kNumberSpaceCount; s++) {
    if (!unacked_packets_[s].empty()) {
        has_ack_eliciting_in_flight = true; break;
    }
}
if (has_ack_eliciting_in_flight) {
    // 用刚归零过的 pto_count_ 重置 → 一个不带退避的 PTO
} else if (!handshake_complete_) {
    // 握手期即使 in-flight 为空也要保活 PTO（防 server 反放大限速）
} else {
    // 握手后且全部清空：cancel，符合 §6.2.1
}
```

`unacked_packets_[]` 只装 ack-eliciting 包（`OnPacketSend` 一开头就 early return 了 ACK-only），所以"空"等同于"无 ack-eliciting in flight"。

### 4.3 PTO 到期做什么（RFC 9002 §6.2.4）

`OnPTOTimer` 的核心动作分三段：

```cpp
rtt_calculator_.OnPTOExpired();   // 退避 +1

// 1) 选最早 unacked → 标 lost → 走重传路径
for (int ns = 0; ns < kNumberSpaceCount; ns++) {
    if (!unacked_packets_[ns].empty()) {
        auto it = unacked_packets_[ns].begin();   // 最早的
        if (it->second.packet && !it->second.is_lost) {
            it->second.is_lost = true;
            lost_packets_.push_back({...});
            congestion_control_->OnPacketLost({...});
            packet_lost_cb_(it->second.packet);
            found_retransmit = true; break;
        }
    }
}

// 2) 握手期且没东西可重传 → 让 connection 注入 PING（Initial/Handshake）
if (!found_retransmit && !handshake_complete_ && probe_needed_cb_) {
    probe_needed_cb_();
}

// 3) 握手后 always 注入一个 1-RTT PING（Bug-19 fix）
if (handshake_complete_ && application_probe_cb_) {
    application_probe_cb_();   // 即使第 1 步成功也照发
}

// 重新调度 pto_timer_ = PTO × 2^pto_count_
```

注意第 3 步的"无条件 PING"——这是 §6.2.4 *probe MUST be ack-eliciting* 的工程兑现：如果路径丢包率高到把重传也吞了，原始重传永远到不了对端，loss detection 这一端就推不动。一个 22 字节的 PING 包不被 stream FC 卡，给 PTO 探测一条**独立**的成功机会，把 transfer-5MB / quicx-quic-go 那种"一直在重传被丢的重传"死锁打开。

两个回调挂在 `BaseConnection` 构造函数里：

```cpp
// connection_base.cpp:81
send_manager_.GetSendControl().SetProbeNeededCallback([this]() {
    auto ping = std::make_shared<PingFrame>();
    ToSendFrame(ping);
});
send_manager_.GetSendControl().SetApplicationProbeCallback([this]() {
    auto ping = std::make_shared<PingFrame>();
    ToSendFrame(ping);
    ActiveSend();   // 同线程把 worker 拨进 send loop，否则 PING 只是入了 wait_frame_list_
});
```

---

## 5. 重传执行：`BaseConnection::TrySendRetransmit`

实现：[`src/quic/connection/connection_base.cpp:1658`](../../../src/quic/connection/connection_base.cpp)。它和 `TrySendNew` 通过 `BaseConnection::TrySend` 派发——`send_control.NeedReSend()` 为 true（即 `lost_packets_` 非空）就走重传分支。

### 5.1 RFC 9000 §13.3：重传 = 新 PN + 老载荷

```cpp
auto lost_entry = lost_packets.front();    // 携带 packet + stream_data
lost_packets.pop_front();
auto lost_pkt = lost_entry.packet;

// 1) 取当前加密级别的 cryptographer（可能 keys 已被 discard → 直接放弃）
auto cryptographer = connection_crypto_.GetCryptographer(lost_pkt->GetCryptoLevel());
if (!cryptographer) return !lost_packets.empty();   // 试下一个

// 2) cwnd 检查；不够就回插队首
if (send_manager_.GetAvailableWindow() == 0) {
    lost_packets.push_front(lost_entry);
    send_manager_.SetCwndLimited();
    return false;
}

// 3) 分配新 PN
uint64_t new_pn = send_manager_.GetPacketNumber().NextPacketNumber(ns);
lost_pkt->SetPacketNumber(new_pn);
lost_pkt->GetHeader()->SetPacketNumberLength(PacketNumber::GetPacketNumberLength(new_pn));
lost_pkt->SetCryptographer(cryptographer);

// 4) RFC 9001 §6.5：重传必须用 *当前* KeyPhase 重新加密（Short header only）
if (lost_pkt->GetHeader()->GetHeaderType() == PacketHeaderType::kShortHeader) {
    lost_pkt->GetHeader()->GetShortHeaderFlag().SetKeyPhase(connection_crypto_.GetCurrentKeyPhase());
}

// 5) 重新编码 + AEAD
auto buffer = std::make_shared<common::SingleBlockBuffer>(...);
if (!lost_pkt->Encode(buffer)) return false;

// 6) 关键：用 *新 PN* 但 *原 stream_data* 重新登记进 unacked_packets_
//    这样下次新 PN 被 ACK 时，stream_data_ack_cb_ 才能把 byte range 反馈给 SendStream
send_control.OnPacketSend(now, lost_pkt, encoded_size, lost_entry.stream_data);

// 7) 发出去
return SendBuffer(buffer);
```

### 5.2 几个易错点

1. **stream_data 必须随包迁移**。`LostPacketEntry { packet, stream_data }` 把原始 frame 的 byte range 一起带过来，新 PN 登记时把它原封不动透回 `unacked_packets_[ns][new_pn]`。如果丢失这一步，retransmit 后那个包再被 ACK，`stream_data_ack_cb_` 就什么都不知道，`SendStream` 的 selective ACK bookkeeping 永远缺一段——这是 quicx 早期 5 MB 文件传输 hang 死的根因。
2. **KeyPhase 必须用当前的**。Long header（Initial/Handshake/0-RTT）没有 KeyPhase 字段，跳过；Short header 不取当前 phase 而沿用包对象里旧 phase 的话，peer AEAD 验证会失败、包被静默丢，loss detector 无尽循环、PN 爆炸到 130k 以上、最终 idle timeout。
3. **cwnd 不够要 push_front**。把已经 pop 出来的 entry 放回队首，否则下次 `TrySendRetransmit` 看到的就是更新的、不该先发的那个。

### 5.3 `DiscardPacketNumberSpace`：握手完成后的清场

[`send_control.cpp:508`](../../../src/quic/connection/controler/send_control.cpp)。当 Initial/Handshake keys 被丢弃（RFC 9000 §4.10），那些 PN space 里的 unacked / lost 都失去了重传可能（peer 已无法解密）：

```cpp
void SendControl::DiscardPacketNumberSpace(PacketNumberSpace ns) {
    for (auto& pair : unacked_packets_[ns]) timer_->RemoveTimer(pair.second.timer_task_);
    unacked_packets_[ns].clear();
    for (auto it = lost_packets_.begin(); it != lost_packets_.end(); ) {
        if (...同 ns...) it = lost_packets_.erase(it); else ++it;
    }
    pkt_num_largest_sent_[ns] = 0;
    pkt_num_largest_acked_[ns] = 0;
    largest_sent_time_[ns] = 0;
}
```

在 `connection_client.cpp:307` 与 `connection_server.cpp:168` 各调一次，对应客户端 / 服务端在 1-RTT 就绪后的 Initial+Handshake key drop。同一时刻 `RecvControl::DiscardPacketNumberSpace` 也被调用，对称清理接收侧的 ACK 状态。

---

## 6. 三种"包级定时器"的语义对照

quicX 里跟 loss recovery 有关的定时器一共三类，容易混淆，集中对照如下：

| 名字 | 范围 | 时长 | 到期做什么 | 取消时机 |
| :--- | :--- | :--- | :--- | :--- |
| per-packet `timer_task_`（每个 unacked 包一个） | 单包 | `GetPTOWithBackoff()` | 把这个包标 lost；CC 减 cwnd；触发重传回调 | 该包被 ACK 时 |
| 全局 `pto_timer_`（每个连接一个） | 跨 PN space | `GetPTOWithBackoff()` | 选最早 unacked 标 lost + 注入 PING | ACK 到达时（重置或 cancel） |
| `ack_timer_`（接收侧，见 `recv_control.h`） | 单 PN space | `max_ack_delay_`（25 ms 默认） | 强制把累积的 ACK 立即送出 | 一旦发出 ACK |

三者解决的是**不同维度**的"沉默"：

- per-packet：单包级别的"很久没回 ACK"；
- pto_timer：连接级别的"对端是不是死了"；
- ack_timer：本端"该回 ACK 了，别再攒了"，与本文主线对称、不参与 loss detection。

---

## 7. 关键不变量

整理几条贯穿全链路的不变量，便于排查：

1. **`unacked_packets_[ns]` 只装 ack-eliciting 包**。`OnPacketSend` 在 line 69 早 return，让 ACK-only 包不入表。这条不变量被 `OnPacketAck` 末尾的"PTO 是否需要 in-flight"判定隐式依赖，违反它会导致 PTO 在没有 ack-eliciting 数据的情况下空转。
2. **`is_lost` 是 per-packet 与 DetectLostPackets 之间的去重锁**。两条路径都会先检查再 set，保证 CC 层的 `OnPacketLost` 对同一个 PN 只调用一次（避免 bytes_in_flight 下溢）。
3. **重传 PN 必须重新登记 stream_data**。`TrySendRetransmit::OnPacketSend(new_pn, lost_entry.stream_data)` 是这条链路的唯一入口，缺这一步会让 retransmit 的 ACK 无法回流给 SendStream。
4. **`pto_count_` 由 PTO 单点维护**。per-packet timer_task_ 不调 `OnPTOExpired()`（注释 line 103 显式说明）——一次大面积丢包可能让 6 个 packet timer 同时开火，如果都自增退避，`pto_count_` 会瞬间打到 6 倍，下次真 PTO 就被错误地拉长 64 倍。
5. **PTO 计算路径上的 max_ack_delay 必经 `GetEffectiveMaxAckDelay()`**。四个调用点（OnPacketSend 每包定时器 / OnPacketSend pto_timer_ rearm / OnPacketAck rearm / OnPTOTimer rearm）全部走这个 accessor，握手未完成时返回 0，符合 §6.2.1。
6. **被丢弃的 PN space 里没有 lost / unacked 残留**。`DiscardPacketNumberSpace` 是单一清场入口，遗漏会让 PTO 在已废弃 keys 的 PN space 上无尽重传。

---

## 8. 关联文档

- [`packet_lifecycle.md`](packet_lifecycle.md) §6.3 —— 接收侧 ACK 触发与 frame 派发，是本文的反面；`RecvControl::ShouldSendImmediateAck` 决定多快回 ACK 直接影响这里 RTT 样本的密度。
- [`handshake_state_machine.md`](handshake_state_machine.md) —— 解释 `handshake_complete_` 何时为 true、`DiscardPacketNumberSpace` 在哪条状态边触发。
- `docs/internal/perf_e2e_analysis.md` §6 —— P3 旋钮（`SetDefaultInitialRtt`）的来历与适用边界。
- `src/quic/congestion_control/` —— `OnPacketSent / OnPacketAcked / OnPacketLost` 的接收方；本文有意不涉及 CC 算法本身。

---

## 9. 关联 RFC

- [RFC 9002 §5](https://www.rfc-editor.org/rfc/rfc9002.html#name-estimating-the-round-trip-t) —— Estimating the Round-Trip Time（`RttCalculator::UpdateRtt`）
- [RFC 9002 §5.3](https://www.rfc-editor.org/rfc/rfc9002.html#name-estimating-smoothed_rtt-and) —— `min(ack_delay, peer_max_ack_delay)` 规则（已知差异，路线图 §2 登记）
- [RFC 9002 §6.1.1](https://www.rfc-editor.org/rfc/rfc9002.html#name-packet-threshold) —— Packet Threshold（`kPacketThreshold = 3`）
- [RFC 9002 §6.1.2](https://www.rfc-editor.org/rfc/rfc9002.html#name-time-threshold) —— Time Threshold（`9/8 · SRTT`）
- [RFC 9002 §6.2.1](https://www.rfc-editor.org/rfc/rfc9002.html#name-computing-pto) —— Computing PTO（`GetPT0Interval` / `GetEffectiveMaxAckDelay`）
- [RFC 9002 §6.2.2.1](https://www.rfc-editor.org/rfc/rfc9002.html#name-before-address-validation) —— 握手期 PTO 探测（`probe_needed_cb_`）
- [RFC 9002 §6.2.4](https://www.rfc-editor.org/rfc/rfc9002.html#name-sending-probe-packets) —— PTO probe 必须 ack-eliciting（`application_probe_cb_` / Bug-19 PING）
- [RFC 9000 §13.3](https://www.rfc-editor.org/rfc/rfc9000.html#name-retransmission-of-information) —— Retransmission semantics（重传 = 新 PN + 老 frame）
- [RFC 9000 §4.10](https://www.rfc-editor.org/rfc/rfc9000.html#name-packet-number-encoding-and-) / [RFC 9001 §4.9](https://www.rfc-editor.org/rfc/rfc9001.html#name-discarding-unused-keys) —— Discard packet number spaces（key drop 后清场）
- [RFC 9001 §6.5](https://www.rfc-editor.org/rfc/rfc9001.html#name-receiving-with-different-ke) —— 重传必须用当前 KeyPhase 重新加密
