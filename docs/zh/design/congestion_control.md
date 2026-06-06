# 拥塞控制：Reno / Cubic / BBR 的统一抽象

本文档梳理 quicX 中拥塞控制（congestion control，下称 CC）模块的总体设计：**一个 `ICongestionControl` 接口 + 一个 `IPacer` 接口 + 5 个实现（Reno、Cubic、BBR v1/v2/v3）**，以及它们与 `SendControl` 的耦合点。本文尝试回答以下问题：

- "为什么 CC 模块和 Loss Recovery 模块要拆开"——它们同处 `connection/controler/`，但分别活在不同接口背后；
- 5 种算法的**共同骨架** 和**差异点**，在 cc_simulator 跑出诡异曲线时知道去看哪个文件；
- 区分**判丢**（loss detection，谁判出来）与**响应**（cwnd 怎么收缩），这两件事在文档里常被混为一谈，源码里它们由两个完全不同的类负责。

这里不重复展开 RFC 9002 §5 / §6 已经写在 [`loss_recovery.md`](loss_recovery.md) 里的判丢路径；只覆盖**判丢之后**的 cwnd / pacing 调整。

---

## 1. 总览：一接口、两扇门、五实现

```text
═══════════════════════════════════════ 调用方 ═══════════════════════════════════════

  SendControl                                         src/quic/connection/controler/send_control.cpp
        │
        ├── 工厂构造（构造函数中读 quic/config.h 的 kDefaultCongestionControl 字符串常量）
        │     字符串("reno"/"cubic"/"bbrv1"/"bbrv2"/"bbrv3") 映射到 CongestionControlType 枚举
        │     → CreateCongestionControl(kCubic | kBbrV1 | kBbrV2 | kBbrV3 | kReno)
        │     注意：CC 算法不* transport_param 协商，是本端编译期/静态配置选择
        │
        ├── 发送时   ──► OnPacketSent(SentPacketEvent { pn, bytes, sent_time, is_retransmit })
        ├── 收 ACK   ──► OnPacketAcked(AckEvent     { pn, bytes_acked, ack_time, ecn_ce, ack_delay,
        │                                              acked_packet_send_time })
        ├── 判丢后   ──► OnPacketLost (LossEvent     { pn, bytes_lost, lost_time })
        ├── 量 RTT   ──► OnRoundTripSample(latest_rtt, ack_delay)
        │
        └── 发包前   ──► CanSend(now, &can_send_bytes) → kOk | kBlockedByCwnd | kBlockedByPacing
              │
              └── 内部：cwnd 限流 + pacer 限速

═══════════════════════════════════════ 接口 ═══════════════════════════════════════

  ICongestionControl                                  src/quic/congestion_control/if_congestion_control.h
        │  纯虚 14 个接口；事件入口 4 个，查询入口 8 个，配置 + qlog 各 1
        │  事件结构（共 3 个 POD）：
        │     SentPacketEvent / AckEvent / LossEvent —— 都裸传 bytes + 时间戳，不带任何 frame 语义
        ▼
  IPacer                                              src/quic/congestion_control/if_pacer.h
        │  CC 输出 pacing_rate_bps，pacer 决定 "下一个包什么时候能发"
        └── 唯一实现：NormalPacer（token-bucket 风格 + 256KB burst budget）

═══════════════════════════════════════ 五个实现 ═══════════════════════════════════════

  ┌── reno_congestion_control.{h,cpp}      Reno (RFC 9002 §B.4 + RFC 5681)
  │     · 最简，约 230 行
  │     · 慢启动：cwnd += bytes_acked
  │     · 拥塞避免：cwnd += MSS²/cwnd
  │     · 丢包：cwnd *= cfg.beta（默认 0.5）
  │     · 教学样本，cc_simulator 的对照基准
  │
  ├── cubic_congestion_control.{h,cpp}     Cubic (RFC 9438 + HyStart++ RFC 9406)
  │     · ~380 行
  │     · 增长函数：W(t) = C·(t − K)³ + W_max  在 packet 域计算
  │     · 丢包：cwnd *= 0.7（β_cubic）
  │     · Reno-friendly：W_cubic 与 W_reno 取 max
  │     · Fast Convergence：cwnd 连续下降时 W_max 再压 (2−β)/2 = 0.85
  │     · HyStart++：在首次丢包之前用 RTT 抬升 / ACK train 提前退出 SS
  │
  ├── bbr_v1_congestion_control.{h,cpp}    BBR v1 (Cardwell et al. ACM Queue 2016)
  │     · 状态机：STARTUP → DRAIN → PROBE_BW → PROBE_RTT
  │     · max-BW 滤波器：10 个样本的 deque
  │     · STARTUP gain = 2/ln2 ≈ 2.885，PROBE_BW 周期切换 [1.25, 0.75, 1, 1, 1, 1, 1, 1]
  │     · cwnd = BDP × cwnd_gain，丢包**不减 cwnd**（这点跟 Reno/Cubic 根本不同）
  │
  ├── bbr_v2_congestion_control.{h,cpp}    BBR v2
  │     · 在 v1 基础上加 inflight_hi / inflight_lo 上下界
  │     · per-round loss_event_count，作为 ProbeBW 收缩信号
  │     · 仍保留 v1 四态机
  │
  └── bbr_v3_congestion_control.{h,cpp}    BBR v3
        · ProbeBW 拆 4 子态：Down → Cruise → Refill → Up
        · 显式 loss_thresh = 2%，越限 cwnd *= beta_loss(0.9)
        · ECN-CE 越限 cwnd *= beta_ecn(0.85)
        · per-round delivered/lost 字节累计
```

> **不在本接口里的东西**：
 - 判丢（`SendControl::DetectLostPackets` + per-packet `timer_task_`）
 - PTO（`SendControl::OnPTOTimer`）
 - RTT 估计（`RttCalculator`）  
 
 这三件事**先于** CC 发生，CC 只接受它们的结果作为 `LossEvent` / `AckEvent` 输入。详见 [`loss_recovery.md`](loss_recovery.md) §3-§4。

---

## 2. 公共抽象：`ICongestionControl`

实现：[`src/quic/congestion_control/if_congestion_control.h`](../../../src/quic/congestion_control/if_congestion_control.h)

### 2.1 三个事件 POD

CC 接口刻意**不传 frame 也不传 packet 对象**，只传字节数和时间戳。这样 CC 实现可以独立单测，也能直接接入 cc_simulator 用合成事件回放。

| 事件 | 字段 | 关键含义 |
| :--- | :--- | :--- |
| `SentPacketEvent` | `pn / bytes / sent_time / is_retransmit` | `is_retransmit` 目前只有 BBR 的 round 边界判定会用 |
| `AckEvent` | `pn / bytes_acked / ack_time / ack_delay / ecn_ce / acked_packet_send_time` | `acked_packet_send_time` 是修 cwnd 折叠 bug 的关键（见 §3.4） |
| `LossEvent` | `pn / bytes_lost / lost_time` | 单个 PN 一次回调；批量丢包要循环调 |

### 2.2 配置 `CcConfigV2`

```cpp
struct CcConfigV2 {
    uint64_t initial_cwnd_bytes = 10 * 1460;   // RFC 9002 §7.2: 10 × MSS 起步
    uint64_t min_cwnd_bytes     = 2  * 1460;   // 下限；防止退化到无法发包
    uint64_t max_cwnd_bytes     = 1000 * 1460; // 上限；防止离谱大
    uint64_t mss_bytes          = 1460;        // 包大小基准
    double   beta               = 0.5;         // Reno 的 cwnd *= beta on loss；Cubic 内部硬编 0.7
    bool     ecn_enabled        = false;       // 预留位；当前 ECN-CE 由 AckEvent.ecn_ce 直接进算法
};
```

注意：**`beta` 字段只对 Reno 生效**。Cubic 的 0.7、BBRv3 的 0.9/0.85 都硬编码在算法内部，因为它们与各自的"恢复曲线"耦合太紧，统一可调反而容易让算法跑出反直觉行为。

### 2.3 三个查询 + `CanSend`

```cpp
SendState CanSend(uint64_t now, uint64_t& can_send_bytes) const;
//   返回 kOk / kBlockedByCwnd / kBlockedByPacing
//   can_send_bytes = max(0, cwnd - bytes_in_flight)

uint64_t  GetCongestionWindow() / GetBytesInFlight() / GetSsthresh();
uint64_t  GetPacingRateBytesPerSec();   // 喂给 IPacer
uint64_t  NextSendTime(uint64_t now);   // pacer 视角的下一个允许发送时刻

bool      InSlowStart() / InRecovery();  // 仅可观测
```

`SendControl::SendBuffer` 在每次发包前调 `CanSend`：cwnd 不够就排队等 ACK，pacer 不够就让 worker 在 `NextSendTime` 重新唤醒。

---

## 3. Reno：最简实现，理解骨架

实现：[`reno_congestion_control.cpp`](../../../src/quic/congestion_control/reno_congestion_control.cpp)（约 230 行）

Reno 在 quicX 里的角色不是"生产推荐"——而是**对照样本**：用最少的代码（一条增长公式 + 一条收缩公式 + recovery period）跑通整个事件接口。所有 5 种算法的"骨架"长得都一样，只是公式和状态机不同。

### 3.1 三段公式

```cpp
// 慢启动 (cwnd < ssthresh)
cwnd += bytes_acked;                                     // 指数增长，每 RTT 翻倍

// 拥塞避免 (cwnd >= ssthresh)
cwnd += (MSS * MSS) / max(cwnd, 1);                      // ≈ 每 RTT 加 1 MSS

// 丢包
ssthresh = cwnd * cfg.beta;                              // 默认 *0.5
cwnd     = max(min_cwnd, ssthresh);                      // 不可低于 min_cwnd
```

### 3.2 Recovery period（RFC 9002 §7.3.2）

`EnterRecovery(now)` 设 `recovery_start_time_ = now`，期间所有后续 ACK **都不增 cwnd**。退出条件是收到一个 **send_time > recovery_start_time** 的 ACK——即"一个完整 RTT 之后才退"。

### 3.3 ECN-CE = 一次"软丢包"

```cpp
if (ev.ecn_ce) {
    if (!in_recovery_) EnterRecovery(ev.ack_time);
    UpdatePacingRate();
    return;
}
```

收到 ECN-CE 标记的 ACK 直接走 `EnterRecovery`，效果等价于丢一个包但不浪费传输代价。Cubic / BBR 也都接 `AckEvent.ecn_ce`，但响应力度不同。

### 3.4 历史踩雷：用 `ack_time` 当作"send time" 退出 recovery

`reno_congestion_control.cpp:73` 那段长注释来自一次真实 bug：

```cpp
// RFC 9002 §7.3.2: exit recovery only when an ACK is received for a
// packet sent AFTER the start of the recovery period. Compare against
// the acked packet's send time (not the ACK arrival time), otherwise
// any ACK exits recovery immediately and a subsequent loss triggers
// another cwnd halving — leading to repeated cwnd collapse.
if (ev.acked_packet_send_time > recovery_start_time_) {
    in_recovery_ = false;
}
```

错误版本用 `ev.ack_time > recovery_start_time_`——任何在 recovery 之后到达的 ACK（即使确认的是 recovery 之前发出的旧包）都会让 recovery 立即退出，紧接着下一次丢包再次触发 `cwnd /= 2`，循环坍缩。这就是为什么 `AckEvent` 里专门带了 `acked_packet_send_time`。Cubic 的 `OnPacketAcked` 里有同样的修复。

---

## 4. Cubic：增长曲线 + HyStart++

实现：[`cubic_congestion_control.cpp`](../../../src/quic/congestion_control/cubic_congestion_control.cpp)（约 380 行）

### 4.1 W_cubic 公式（RFC 9438 §4.2）

```
W_cubic(t) = C · (t − K)³ + W_max
其中：
   C        = 0.4         （kCubicC，缩放常量）
   W_max    = 上次拥塞事件时的 cwnd（packets 域）
   K        = ∛((W_max − W_cwnd) / C)   epoch 启动时计算一次
   t        = 当前时间 − epoch_start    单位秒
```

代码就在 `IncreaseOnAck`：

```cpp
double t_sec = (now - epoch_start_us_) / 1e6;
double t_k   = std::abs(t_sec - k_time_sec_);   // 注意取绝对值
double w_cubic_pkts = kCubicC * t_k * t_k * t_k + w_max_pkts_;
```

`|t − K|` 取绝对值是因为曲线在 K 点对称：t < K 时是凹的（接近上次拥塞点的"平稳侦察"），t > K 时是凸的（已经逼近上限就大胆向上探）。

### 4.2 Reno-friendly region（RFC 9438 §4.3）

短 RTT 路径上 W_cubic 反而比 Reno 涨得慢。为了不在 LAN 上跑输给 Reno 流，Cubic 同时算一个 `W_reno = w_last + 1.5 · bytes_acked / w_last`，**取两者较大值**。

### 4.3 Fast Convergence（RFC 9438 §4.7）

`OnPacketLost` 检测到"本次拥塞时 cwnd 比上次还低"——说明网络更挤了，**额外**把 W_max 拉到 `0.85 × W_max`，让新流更快和老流公平。否则 W_max 锚在过去那个不那么挤的值上，新流 ramp 起来慢。

### 4.4 HyStart++（RFC 9406）

`CheckHyStartExit` 在**第一次丢包前**就退出慢启动，避免 Reno 那种"cwnd 已经 256 倍 IW 才碰墙、然后一巴掌减半"的过冲。两个信号：

| 信号 | 阈值 | 含义 |
| :--- | :--- | :--- |
| **RTT 抬升** | per-round min RTT > global min RTT + 4ms | 队列开始堆，bottleneck 已饱和 |
| **ACK train spacing** | 相邻 ACK 间距 > 2ms | delivery rate 已经平台化 |

任一触发：`ssthresh = cwnd_now`，立刻进入拥塞避免。HyStart++ 在 long-fat path（高 BDP）上效果尤其显著。

### 4.5 ECN 路径

Cubic 的 ECN-CE 路径（`OnPacketAcked` 的 `if (ev.ecn_ce)` 分支）做的事比 Reno 更多：直接乘 `kBetaCubic = 0.7` 收 cwnd + 把 W_max 按 Fast Convergence 处理。这是教学子集的工程取舍——RFC 9438 本身没规定 ECN 行为，CUBIC + ECN 的标准在 RFC 8311 / 9000 §13.4 有更精细的语义。

---

## 5. BBR：基于"瓶颈带宽 × min-RTT"建模，而不是丢包

BBR 系列在 quicX 中是**教学子集**，不是 draft-cardwell-iccrg-bbr-congestion-control 的完整 transcript。下面以 v1 为基线，再点出 v2 / v3 的差异。

### 5.1 v1：四态机（[BBR-Queue 2016] §3）

实现：[`bbr_v1_congestion_control.cpp`](../../../src/quic/congestion_control/bbr_v1_congestion_control.cpp)

```text
                  full bw 检测：3 轮 ACK 增长 < 25%
  STARTUP ─────────────────────────────────────────► DRAIN
  pacing 2.885                                        pacing 1/2.885
  cwnd_gain 2.0                                       cwnd_gain 2.0
                                                          │
                                                          │ inflight ≤ BDP
                                                          ▼
  PROBE_RTT ◄────────── 每 10s ──────── PROBE_BW ◄─────────┘
  cwnd 限到 4·MSS 200ms                  pacing cycle
  让 min_rtt 重新可见                    [1.25, 0.75, 1, 1, 1, 1, 1, 1]
                                         cwnd_gain = 2.0
```

**两个关键测量**：

- **max-BW 滤波器**：`bw_window_` deque 保留最近 10 个样本，取 max。一次 sample = `bytes_acked / elapsed`，要求 elapsed ≥ 1 SRTT 避免低估。
- **min-RTT**：`min_rtt_us_` + `min_rtt_stamp_us_`。如果 10s 没刷新，强制 PROBE_RTT 把 cwnd 压到 `4·MSS` 让队列排空、min-RTT 可见。

**cwnd 不响应丢包**：`OnPacketLost` 里只更新 `bytes_in_flight_`。BBR 的核心理念是用带宽测量驱动 cwnd，丢包不是输入信号——这点和 Reno/Cubic 的整个范式都不同。

### 5.2 v2：加 inflight_hi / inflight_lo

实现：[`bbr_v2_congestion_control.cpp`](../../../src/quic/congestion_control/bbr_v2_congestion_control.cpp)

新增字段：

```cpp
uint64_t inflight_hi_bytes_ = UINT64_MAX;   // 最近一次成功探测的 inflight 上限
uint64_t inflight_lo_bytes_ = 0;            // 最近一次拥塞迹象时的下限
uint64_t loss_event_count_in_round_ = 0;    // 本轮丢包事件计数
```

`OnPacketLost` 现在会动 cwnd（轻度），Probe Up 阶段如果 `inflight > inflight_hi` 也会回收。本质是给 v1 加了**显式的 inflight 上下界**，让带宽探测不至于把 buffer 灌爆。

### 5.3 v3：ProbeBW 拆 4 子态

实现：[`bbr_v3_congestion_control.cpp`](../../../src/quic/congestion_control/bbr_v3_congestion_control.cpp)

```cpp
enum class ProbeBwState { kDown, kCruise, kRefill, kUp };
```

| 子态 | pacing_gain | 目的 |
| :--- | :--- | :--- |
| `kDown`   | 0.9  | 排队，让 inflight 跌到 inflight_hi 之下 |
| `kCruise` | 1.0  | 平稳跑在 BDP |
| `kRefill` | 1.0  | 重灌 pipe，准备探测 |
| `kUp`     | 1.25 | 向上探带宽 |

加上**显式丢包/ECN 阈值**：

```cpp
double loss_thresh_ = 0.02;   // round_lost / round_delivered > 2% 触发收缩
double beta_loss_   = 0.9;    // cwnd *= 0.9
double beta_ecn_    = 0.85;   // ECN 更激进，cwnd *= 0.85
```

v3 已经接近"丢包 + 带宽建模"的混合范式，但仍然保留 BBR 的 ProbeBW / ProbeRTT 大轮换。

### 5.4 BBR 共有易踩雷点

1. **min_rtt_stamp_us_ 不刷新 → 死锁在低 cwnd**：如果 ACK 路径漏给 stamp 赋值，10s 后强制 PROBE_RTT 把 cwnd 砸到 `4·MSS` 就一直回不来。修复点见 v1 的 `OnPacketAcked` 行 99-103。
2. **bw 样本窗口太短 → STARTUP 提前退出**：`kBwWindow=10` + 至少 1 SRTT 才记一个样本，是为了避免单个突发 ACK 让 max_bw 假涨然后 full_bw_cnt 提前满 3。
3. **end_of_round_pn_ 永不递增**：导致 round 边界永远不切换、CheckFullBandwidthReached 一直误判。注意 `OnPacketSent` 里 `if (... || ev.pn > end_of_round_pn_)` 的 OR 分支必须存在。

---

## 6. Pacer：CC 的限速出口

实现：[`normal_pacer.{h,cpp}`](../../../src/quic/congestion_control/normal_pacer.cpp)

唯一实现，token-bucket 风格：

| 字段 | 含义 |
| :--- | :--- |
| `pacing_rate_bytes_per_sec_` | CC 调 `OnPacingRateUpdated` 设置 |
| `burst_budget_bytes_` | 当前可用 burst 字节数；初始 = `max_burst_bytes_ = 256KB` |
| `next_send_time_ms_` | burst 用尽后的下一允许发送时刻 |

行为：

- **burst budget 还有** → `CanSend` 直接 true，记账扣 `bytes`；
- **burst 用尽** → `next_send_time_ms_ = sent_time_ms + bytes·1000 / pacing_rate`；
- **闲置时间** → `RefillBurstBudget` 按 `(rate × elapsed_ms) / 1000` 补回，封顶 `max_burst`。

**256KB burst 的来历**：早期是 16KB，结果 LAN 环境单 RTT 内的累计字节远超 16KB，pacer 反复打嗝把吞吐拉到只有 cwnd 上限的 1/3。256KB 是性能基线测试中收敛出来的折中——既能吸收一个完整 cwnd 的瞬时突发，又不至于让 pacing 形同虚设。

> **CanSend 的二选一**：`SendControl::SendBuffer` 在发包前先问 CC 的 `CanSend`（cwnd 限流），再通过 `pacer_->CanSend` 决定要不要立即发还是等到 `NextSendTime`。这两层是**串联**的——任一拒绝都不发。

---

## 7. 与上下游模块的边界

```
┌──────────────────┐    OnPacketSent      ┌──────────────────┐
│   SendControl    │ ───────────────────► │ ICongestionCtrl  │
│  (RFC 9002 §A.4) │ ◄─────CanSend─────── │  (cwnd / pacing) │
│                  │      LossEvent       │                  │
│                  │ ◄──────────────────  │   IPacer         │
└──────────────────┘                       └──────────────────┘
        ▲                                          ▲
        │ packet_lost_cb_                          │ pacing_rate_bps
        │                                          │
┌──────────────────┐    UpdateRtt         ┌──────────────────┐
│  RttCalculator   │ ◄─── OnPacketAck ─── │  RecvControl /   │
│  (RFC 9002 §5)   │                      │  FrameProcessor  │
└──────────────────┘                       └──────────────────┘
```

- **CC 只对 `SendControl` 暴露**：上层不直接持有 `ICongestionControl`，而是通过 `SendControl::CanSend` 拿到聚合后的"能发多少字节"。
- **算法选择是本端的事，不走线**：`SendControl` 构造时读 [`quic/config.h`](../../../src/quic/config.h) 的 `kDefaultCongestionControl` 编译期常量（默认 `"cubic"`），把字符串映射成 `CongestionControlType` 枚举后调 `CreateCongestionControl`。CC 算法**不是** transport_param——RFC 9000 的 transport_param 不包含 CC 算法字段，对端用什么 CC 我端无从知晓也不需要知晓。要切算法：改 `kDefaultCongestionControl`、重编。`SendControl::UpdateConfig(const TransportParam&)` 只读 `max_ack_delay` / `ack_delay_exponent`，与 CC 选择无关。
- **CC 不知道 frame 类型**：它收到的全是字节数。"哪些 frame 算 ack-eliciting 因此进 unacked"是 SendControl 的事，CC 只看到 `OnPacketSent(bytes=…)`。
- **判丢不在 CC 里**：判丢由 `SendControl::DetectLostPackets` + per-packet `timer_task_` 完成，结果通过 `OnPacketLost` 喂给 CC。详见 [`loss_recovery.md`](loss_recovery.md) §3。
- **RTT 不在 CC 里**：CC 通过 `OnRoundTripSample` 接收已经 ack_delay 修正过的样本，自己内部维护 SRTT 只作为 pacing 输入。RTT 估计的权威实现在 `RttCalculator`。

---

## 8. 验证手段：cc_test_framework + 4 个测试

实现：[`test/congestion_control/`](../../../test/congestion_control/)

| 文件 | 角色 |
| :--- | :--- |
| `cc_test_framework.{h,cpp}` | 模拟器骨架；生成合成 ACK / loss 事件流回放给 CC |
| `network_simulator.{h,cpp}` | 简单的链路模型（带宽、RTT、loss rate、buffer size） |
| `cc_algorithm_validation_test.cpp` | 5 种算法的基本不变量（cwnd 单调性 / recovery 不抖 / pacing 收敛） |
| `cc_bbr_detailed_test.cpp` | BBR 状态机分阶段断言 |
| `cc_comprehensive_test.cpp` | 全算法对照跑分 |
| `cc_realistic_network_test.cpp` | 真实网络条件混合（变 RTT / burst loss） |

**这是验证 CC 算法的入口**：要改算法、要排查"为什么 cwnd 跑出诡异曲线"，先在这里跑出可复现的 trace 再回头看实现。

---

## 9. 关键不变量

| # | 不变量 | 失败现象 | 排查指引 |
|---|---|---|---|
| 1 | `bytes_in_flight_` 在 `OnPacketSent` 涨、`OnPacketAcked / OnPacketLost` 减 | cwnd 永远不饱和 / 永远卡 0 | 查是否漏调 `OnPacketLost`（判丢路径见 [`loss_recovery.md`](loss_recovery.md) §3.2） |
| 2 | Recovery 退出靠 `acked_packet_send_time > recovery_start_time_` | cwnd 反复折半坍缩 | 检查 `AckEvent.acked_packet_send_time` 是否被赋值（不是 0） |
| 3 | Cubic 的 `epoch_start_us_ = 0` 表示需要重置 | 拥塞后 cwnd 增长曲线扭曲 | `OnPacketLost` 后 `IncreaseOnAck` 第一次会 `ResetEpoch` |
| 4 | BBR 的 `min_rtt_stamp_us_` 必须随每次 ACK 更新（当 srtt ≤ min_rtt） | 卡死在 PROBE_RTT 出不来 | 查 `OnPacketAcked` 行 99-103 是否被改坏 |
| 5 | Pacer 的 `last_update_ms_` 第一次调用时设为 now（不是 0）| 第一次 refill 把 burst 灌爆 | `RefillBurstBudget` 头几行的 0 哨兵 |
| 6 | `CanSend` 的 cwnd 检查与 pacer 检查**串联** | cwnd 满 / pacing 满任一就发包 → 早期过冲 | `SendControl::SendBuffer` 中两层 if 都要存在 |

---

## 10. 关联文档

- [`packet_lifecycle.md`](packet_lifecycle.md) §6.3 —— 接收侧 ACK 触发策略，是 CC `OnPacketAcked` 的上游。
- [`loss_recovery.md`](loss_recovery.md) §3-§4 —— 判丢与 PTO 的实现，是 CC `OnPacketLost` 的上游。
- [`metrics.md`](metrics.md) —— `BytesInFlight` / `CongestionWindowBytes` / `PacingRateBytesPerSec` / `CongestionEventsTotal` / `SlowStartExits` 的导出位置。
- `test/congestion_control/cc_test_framework.{h,cpp}` —— 验证框架。

---

## 11. 关联 RFC / 论文

- **RFC 9002 §7** *Pluggable Congestion Control*：QUIC 允许任意 CC，事件接口的语义来源。  
  https://datatracker.ietf.org/doc/html/rfc9002#section-7
- **RFC 9002 §B.4** NewReno for QUIC（伪代码）。
- **RFC 5681** TCP Congestion Control：Reno 的原始定义。
- **RFC 9438** *CUBIC for Fast and Long-Distance Networks*：Cubic 标准。  
  https://datatracker.ietf.org/doc/html/rfc9438
- **RFC 9406** *HyStart++*：Cubic 的早退出启发式。  
  https://datatracker.ietf.org/doc/html/rfc9406
- **Cardwell, Cheng, Gunn, Yeganeh, Jacobson, "BBR: Congestion-Based Congestion Control"**, ACM Queue 14(5), 2016：BBR v1 原始论文。
- **draft-cardwell-iccrg-bbr-congestion-control-02**：BBR 的 IETF 表达。
- **RFC 8311 §4.2** ECN-CE 反馈语义；本仓库 5 个算法都接 `AckEvent.ecn_ce`。
- **RFC 3168** ECN 总论。
