# Congestion Control: The Unified Abstraction over Reno / Cubic / BBR

This document walks through the overall design of the congestion control (CC) module in quicX: **one `ICongestionControl` interface + one `IPacer` interface + 5 implementations (Reno, Cubic, BBR v1/v2/v3)**, and their coupling points with `SendControl`. It tries to answer:

- "Why the CC module and the Loss Recovery module are split apart" — they both live under `connection/controller/`, but behind different interfaces;
- The **shared skeleton** and **differences** of the 5 algorithms, so you know which file to look at when cc_simulator produces a weird curve;
- Distinguishing **loss detection** (who judges a packet lost) from **response** (how cwnd shrinks) — two things often conflated in documentation but owned by two entirely different classes in the source.

This document doesn't re-expand the loss-detection paths of RFC 9002 §5 / §6 already covered in [`loss_recovery.md`](loss_recovery.md); it covers only the cwnd / pacing adjustments **after** a loss verdict.

---

## 1. Overview: One Interface, Two Gates, Five Implementations

```text
═══════════════════════════════════════ Callers ═══════════════════════════════════════

  SendControl                                         src/quic/connection/controller/send_control.cpp
        │
        ├── factory construction (constructor reads the kDefaultCongestionControl string
        │     constant in quic/config.h)
        │     string ("reno"/"cubic"/"bbrv1"/"bbrv2"/"bbrv3") mapped to the CongestionControlType enum
        │     → CreateCongestionControl(kCubic | kBbrV1 | kBbrV2 | kBbrV3 | kReno)
        │     note: the CC algorithm is NOT negotiated via transport_param; it is a
        │     compile-time / static local configuration choice
        │
        ├── on send     ──► OnPacketSent(SentPacketEvent { pn, bytes, sent_time, is_retransmit })
        ├── on ACK      ──► OnPacketAcked(AckEvent     { pn, bytes_acked, ack_time, ecn_ce, ack_delay,
        │                                              acked_packet_send_time })
        ├── after loss  ──► OnPacketLost (LossEvent     { pn, bytes_lost, lost_time })
        ├── RTT sample  ──► OnRoundTripSample(latest_rtt, ack_delay)
        │
        └── before send ──► CanSend(now, &can_send_bytes) → kOk | kBlockedByCwnd | kBlockedByPacing
              │
              └── internally: cwnd limiting + pacer rate limiting

═══════════════════════════════════════ Interfaces ═══════════════════════════════════════

  ICongestionControl                                  src/quic/congestion_control/if_congestion_control.h
        │  14 pure-virtual methods; 4 event entries, 8 query entries, 1 config + 1 qlog
        │  event structs (3 PODs total):
        │     SentPacketEvent / AckEvent / LossEvent —— all pass bare bytes + timestamps,
        │     carrying no frame semantics
        ▼
  IPacer                                              src/quic/congestion_control/if_pacer.h
        │  CC outputs pacing_rate_bps; the pacer decides "when the next packet may go out"
        └── sole implementation: NormalPacer (token-bucket style + 256KB burst budget)

═══════════════════════════════════════ Five implementations ═══════════════════════════════════════

  ┌── reno_congestion_control.{h,cpp}      Reno (RFC 9002 §B.4 + RFC 5681)
  │     · simplest, ~230 lines
  │     · slow start: cwnd += bytes_acked
  │     · congestion avoidance: cwnd += MSS²/cwnd
  │     · loss: cwnd *= cfg.beta (default 0.5)
  │     · the minimal baseline and cc_simulator's reference baseline
  │
  ├── cubic_congestion_control.{h,cpp}     Cubic (RFC 9438 + HyStart++ RFC 9406)
  │     · ~380 lines
  │     · growth function: W(t) = C·(t − K)³ + W_max  computed in the packet domain
  │     · loss: cwnd *= 0.7 (β_cubic)
  │     · Reno-friendly: max(W_cubic, W_reno)
  │     · Fast Convergence: on repeated cwnd decline, W_max further pressed to (2−β)/2 = 0.85
  │     · HyStart++: exits SS before the first loss using RTT inflation / ACK train
  │
  ├── bbr_v1_congestion_control.{h,cpp}    BBR v1 (Cardwell et al. ACM Queue 2016)
  │     · state machine: STARTUP → DRAIN → PROBE_BW → PROBE_RTT
  │     · max-BW filter: a deque of 10 samples
  │     · STARTUP gain = 2/ln2 ≈ 2.885; PROBE_BW cycles [1.25, 0.75, 1, 1, 1, 1, 1, 1]
  │     · cwnd = BDP × cwnd_gain; loss does **not** shrink cwnd (fundamentally unlike Reno/Cubic)
  │
  ├── bbr_v2_congestion_control.{h,cpp}    BBR v2
  │     · adds inflight_hi / inflight_lo bounds on top of v1
  │     · per-round loss_event_count as a ProbeBW shrink signal
  │     · still keeps v1's four-state machine
  │
  └── bbr_v3_congestion_control.{h,cpp}    BBR v3
        · ProbeBW split into 4 sub-states: Down → Cruise → Refill → Up
        · explicit loss_thresh = 2%; over it, cwnd *= beta_loss (0.9)
        · ECN-CE over threshold: cwnd *= beta_ecn (0.85)
        · per-round delivered/lost byte accounting
```

> **What is NOT in this interface**:
> - Loss detection (`SendControl::DetectLostPackets` + the per-packet `timer_task_`)
> - PTO (`SendControl::OnPTOTimer`)
> - RTT estimation (`RttCalculator`)

These three happen **before** CC; CC only accepts their results as `LossEvent` / `AckEvent` inputs. See [`loss_recovery.md`](loss_recovery.md) §3-§4.

---

## 2. The Common Abstraction: `ICongestionControl`

Implementation: [`src/quic/congestion_control/if_congestion_control.h`](../../../src/quic/congestion_control/if_congestion_control.h)

### 2.1 The Three Event PODs

The CC interface deliberately **passes neither frames nor packet objects** — only byte counts and timestamps. This lets CC implementations be unit-tested standalone and be driven directly by cc_simulator with synthetic event replay.

| Event | Fields | Key Meaning |
| :--- | :--- | :--- |
| `SentPacketEvent` | `pn / bytes / sent_time / is_retransmit` | `is_retransmit` is currently used only by BBR's round-boundary detection |
| `AckEvent` | `pn / bytes_acked / ack_time / ack_delay / ecn_ce / acked_packet_send_time` | `acked_packet_send_time` is the key to the cwnd-collapse fix (see §3.4) |
| `LossEvent` | `pn / bytes_lost / lost_time` | one callback per PN; batch loss must loop the calls |

### 2.2 Configuration: `CcConfigV2`

```cpp
struct CcConfigV2 {
    uint64_t initial_cwnd_bytes = 10 * 1460;   // RFC 9002 §7.2: start at 10 × MSS
    uint64_t min_cwnd_bytes     = 2  * 1460;   // lower bound; prevents degrading to unsendable
    uint64_t max_cwnd_bytes     = 1000 * 1460; // upper bound; prevents absurd sizes
    uint64_t mss_bytes          = 1460;        // packet-size basis
    double   beta               = 0.5;         // Reno's cwnd *= beta on loss; Cubic hard-codes 0.7 internally
    bool     ecn_enabled        = false;       // reserved; currently ECN-CE enters the algorithm
                                                // directly via AckEvent.ecn_ce
};
```

Note: **the `beta` field applies to Reno only**. Cubic's 0.7 and BBRv3's 0.9/0.85 are hard-coded inside the algorithms, because they couple too tightly with their respective recovery curves; making them uniformly tunable would easily drive the algorithms into counter-intuitive behavior.

### 2.3 Three Queries + `CanSend`

```cpp
SendState CanSend(uint64_t now, uint64_t& can_send_bytes) const;
//   returns kOk / kBlockedByCwnd / kBlockedByPacing
//   can_send_bytes = max(0, cwnd - bytes_in_flight)

uint64_t  GetCongestionWindow() / GetBytesInFlight() / GetSsthresh();
uint64_t  GetPacingRateBytesPerSec();   // fed to IPacer
uint64_t  NextSendTime(uint64_t now);   // the next allowed send instant, from the pacer's view

bool      InSlowStart() / InRecovery();  // observability only
```

`SendControl::SendBuffer` calls `CanSend` before every send: cwnd short → queue and wait for ACK; pacer short → let the worker re-wake at `NextSendTime`.

---

## 3. Reno: The Minimal Implementation, for Understanding the Skeleton

Implementation: [`reno_congestion_control.cpp`](../../../src/quic/congestion_control/reno_congestion_control.cpp) (~230 lines)

Reno's role in quicX is not "production recommendation" — it is the **reference sample**: with the least code (one growth formula + one shrink formula + a recovery period) it exercises the entire event interface. All 5 algorithms share the same skeleton shape; only the formulas and state machines differ.

### 3.1 Three Formula Segments

```cpp
// slow start (cwnd < ssthresh)
cwnd += bytes_acked;                                     // exponential growth, doubling per RTT

// congestion avoidance (cwnd >= ssthresh)
cwnd += (MSS * MSS) / max(cwnd, 1);                      // ≈ +1 MSS per RTT

// loss
ssthresh = cwnd * cfg.beta;                              // default *0.5
cwnd     = max(min_cwnd, ssthresh);                      // never below min_cwnd
```

### 3.2 The Recovery Period (RFC 9002 §7.3.2)

`EnterRecovery(now)` sets `recovery_start_time_ = now`; during it, all subsequent ACKs **grow no cwnd**. The exit condition is receiving an ACK with **send_time > recovery_start_time** — i.e. "exit only after one full RTT".

### 3.3 ECN-CE = One "Soft Loss"

```cpp
if (ev.ecn_ce) {
    if (!in_recovery_) EnterRecovery(ev.ack_time);
    UpdatePacingRate();
    return;
}
```

An ACK marked ECN-CE goes straight into `EnterRecovery` — equivalent to losing one packet but without wasting the transmission. Cubic / BBR also consume `AckEvent.ecn_ce`, with differing response strengths.

### 3.4 A Historical Landmine: Using `ack_time` as the "send time" to Exit Recovery

The long comment at `reno_congestion_control.cpp:73` comes from a real bug:

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

The buggy version used `ev.ack_time > recovery_start_time_` — any ACK arriving after recovery started (even one confirming an old packet sent before recovery) exited recovery immediately; the next loss then triggered another `cwnd /= 2`, a collapse loop. That is exactly why `AckEvent` carries `acked_packet_send_time`. Cubic's `OnPacketAcked` carries the same fix.

---

## 4. Cubic: The Growth Curve + HyStart++

Implementation: [`cubic_congestion_control.cpp`](../../../src/quic/congestion_control/cubic_congestion_control.cpp) (~380 lines)

### 4.1 The W_cubic Formula (RFC 9438 §4.2)

```
W_cubic(t) = C · (t − K)³ + W_max
where:
   C        = 0.4         (kCubicC, the scaling constant)
   W_max    = the cwnd at the last congestion event (in the packet domain)
   K        = ∛((W_max − W_cwnd) / C)   computed once at epoch start
   t        = now − epoch_start          in seconds
```

The code sits in `IncreaseOnAck`:

```cpp
double t_sec = (now - epoch_start_us_) / 1e6;
double t_k   = std::abs(t_sec - k_time_sec_);   // note the absolute value
double w_cubic_pkts = kCubicC * t_k * t_k * t_k + w_max_pkts_;
```

`|t − K|` takes the absolute value because the curve is symmetric at K: t < K is concave (gentle reconnaissance near the last congestion point), t > K is convex (boldly probing upward once the limit is near).

### 4.2 The Reno-Friendly Region (RFC 9438 §4.3)

On short-RTT paths W_cubic actually grows slower than Reno. To avoid losing to Reno flows on LANs, Cubic simultaneously computes `W_reno = w_last + 1.5 · bytes_acked / w_last` and **takes the larger** of the two.

### 4.3 Fast Convergence (RFC 9438 §4.7)

When `OnPacketLost` detects "this congestion event's cwnd is even lower than last time" — the network got more crowded — it **additionally** pulls W_max down to `0.85 × W_max`, letting new flows converge with old ones faster. Otherwise W_max stays anchored at the less-crowded past value and new flows ramp up slowly.

### 4.4 HyStart++ (RFC 9406)

`CheckHyStartExit` exits slow start **before the first loss**, avoiding the Reno-style overshoot of "cwnd hits the wall at 256× IW, then gets slapped down by half". Two signals:

| Signal | Threshold | Meaning |
| :--- | :--- | :--- |
| **RTT inflation** | per-round min RTT > global min RTT + 4ms | queues are building; the bottleneck is saturated |
| **ACK train spacing** | adjacent ACKs spaced > 2ms apart | the delivery rate has plateaued |

Either trigger: `ssthresh = cwnd_now`, straight into congestion avoidance. HyStart++ is especially effective on long-fat paths (high BDP).

### 4.5 The ECN Path

Cubic's ECN-CE path (the `if (ev.ecn_ce)` branch of `OnPacketAcked`) does more than Reno's: directly multiplies cwnd by `kBetaCubic = 0.7` and treats W_max per Fast Convergence. This is an engineering tradeoff of the minimal implementation — RFC 9438 itself doesn't specify ECN behavior; CUBIC + ECN has finer semantics in RFC 8311 / RFC 9000 §13.4.

---

## 5. BBR: Modeling on "Bottleneck Bandwidth × min-RTT", Not on Loss

The BBR family in quicX is a **minimal implementation**, not a complete transcript of draft-cardwell-iccrg-bbr-congestion-control. Below, v1 is the baseline, then the v2 / v3 deltas.

### 5.1 v1: The Four-State Machine ([BBR-Queue 2016] §3)

Implementation: [`bbr_v1_congestion_control.cpp`](../../../src/quic/congestion_control/bbr_v1_congestion_control.cpp)

```text
                  full-bw detection: 3 rounds of ACK growth < 25%
  STARTUP ─────────────────────────────────────────► DRAIN
  pacing 2.885                                        pacing 1/2.885
  cwnd_gain 2.0                                       cwnd_gain 2.0
                                                          │
                                                          │ inflight ≤ BDP
                                                          ▼
  PROBE_RTT ◄────────── every 10s ──────── PROBE_BW ◄─────────┘
  cwnd capped to 4·MSS for 200ms          pacing cycle
  making min_rtt visible again            [1.25, 0.75, 1, 1, 1, 1, 1, 1]
                                          cwnd_gain = 2.0
```

**Two key measurements**:

- **The max-BW filter**: `bw_window_`, a deque keeping the last 10 samples, taking the max. One sample = `bytes_acked / elapsed`, requiring elapsed ≥ 1 SRTT to avoid underestimation.
- **min-RTT**: `min_rtt_us_` + `min_rtt_stamp_us_`. If not refreshed for 10s, PROBE_RTT is forced, capping cwnd at `4·MSS` to drain the queue and make min-RTT visible again.

**cwnd does not respond to loss**: `OnPacketLost` only updates `bytes_in_flight_`. BBR's core philosophy is driving cwnd with bandwidth measurements; loss is not an input signal — fundamentally different from the entire Reno/Cubic paradigm.

### 5.2 v2: Adding inflight_hi / inflight_lo

Implementation: [`bbr_v2_congestion_control.cpp`](../../../src/quic/congestion_control/bbr_v2_congestion_control.cpp)

New fields:

```cpp
uint64_t inflight_hi_bytes_ = UINT64_MAX;   // inflight cap from the last successful probe
uint64_t inflight_lo_bytes_ = 0;            // lower bound at the last congestion sign
uint64_t loss_event_count_in_round_ = 0;    // loss-event count this round
```

`OnPacketLost` now moves cwnd (mildly), and the Probe Up phase also pulls back if `inflight > inflight_hi`. In essence, v1 gains **explicit inflight bounds**, keeping bandwidth probes from flooding the buffer.

### 5.3 v3: ProbeBW Split into 4 Sub-states

Implementation: [`bbr_v3_congestion_control.cpp`](../../../src/quic/congestion_control/bbr_v3_congestion_control.cpp)

```cpp
enum class ProbeBwState { kDown, kCruise, kRefill, kUp };
```

| Sub-state | pacing_gain | Purpose |
| :--- | :--- | :--- |
| `kDown`   | 0.9  | drain, letting inflight fall below inflight_hi |
| `kCruise` | 1.0  | run steadily at BDP |
| `kRefill` | 1.0  | refill the pipe, preparing to probe |
| `kUp`     | 1.25 | probe bandwidth upward |

Plus **explicit loss/ECN thresholds**:

```cpp
double loss_thresh_ = 0.02;   // round_lost / round_delivered > 2% triggers shrink
double beta_loss_   = 0.9;    // cwnd *= 0.9
double beta_ecn_    = 0.85;   // ECN is more aggressive: cwnd *= 0.85
```

v3 already approaches a "loss + bandwidth modeling" hybrid paradigm, while keeping BBR's big ProbeBW / ProbeRTT rotation.

### 5.4 Common BBR Landmines

1. **min_rtt_stamp_us_ never refreshed → deadlock at low cwnd**: if the ACK path forgets to assign the stamp, the forced PROBE_RTT after 10s smashes cwnd to `4·MSS` and it never comes back. The fix is at v1's `OnPacketAcked`, lines 99-103.
2. **bw sample window too short → STARTUP exits early**: `kBwWindow=10` plus requiring ≥ 1 SRTT per sample exists to keep a single ACK burst from falsely inflating max_bw and prematurely filling full_bw_cnt's 3.
3. **end_of_round_pn_ never increments**: round boundaries never switch and CheckFullBandwidthReached keeps misjudging. Note the OR branch `if (... || ev.pn > end_of_round_pn_)` in `OnPacketSent` must exist.

---

## 6. The Pacer: CC's Rate-Limiting Exit

Implementation: [`normal_pacer.{h,cpp}`](../../../src/quic/congestion_control/normal_pacer.cpp)

The sole implementation, token-bucket style:

| Field | Meaning |
| :--- | :--- |
| `pacing_rate_bytes_per_sec_` | set by CC via `OnPacingRateUpdated` |
| `burst_budget_bytes_` | currently available burst bytes; initial = `max_burst_bytes_ = 256KB` |
| `next_send_time_ms_` | the next allowed send instant once the burst is exhausted |

Behavior:

- **burst budget remaining** → `CanSend` returns true directly, debiting `bytes`;
- **burst exhausted** → `next_send_time_ms_ = sent_time_ms + bytes·1000 / pacing_rate`;
- **idle time** → `RefillBurstBudget` restores `(rate × elapsed_ms) / 1000`, capped at `max_burst`.

**The origin of the 256KB burst**: it was 16KB early on, but in LAN environments the cumulative bytes within a single RTT far exceed 16KB; the pacer hiccuped repeatedly, dragging throughput to 1/3 of the cwnd ceiling. 256KB is the compromise converged upon in performance baseline testing — big enough to absorb a full cwnd's instantaneous burst, not so big as to render pacing meaningless.

> **CanSend's either-or**: before sending, `SendControl::SendBuffer` first asks CC's `CanSend` (cwnd limiting), then decides via `pacer_->CanSend` whether to send immediately or wait until `NextSendTime`. The two layers are **in series** — a rejection from either blocks the send.

---

## 7. Boundaries with Upstream/Downstream Modules

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

- **CC is exposed only to `SendControl`**: upper layers never hold `ICongestionControl` directly; they get the aggregated "how many bytes can be sent" through `SendControl::CanSend`.
- **Algorithm choice is a local matter and never goes on the wire**: at construction `SendControl` reads the `kDefaultCongestionControl` compile-time constant in [`quic/config.h`](../../../src/quic/config.h) (default `"cubic"`), maps the string to the `CongestionControlType` enum and calls `CreateCongestionControl`. The CC algorithm is **not** a transport_param — RFC 9000's transport params carry no CC-algorithm field; what CC the peer uses is unknowable and unnecessary to know. To switch algorithms: change `kDefaultCongestionControl`, rebuild. `SendControl::UpdateConfig(const TransportParam&)` reads only `max_ack_delay` / `ack_delay_exponent`, unrelated to CC selection.
- **CC knows no frame types**: everything it receives is byte counts. "Which frames count as ack-eliciting and thus enter unacked" is SendControl's business; CC only sees `OnPacketSent(bytes=…)`.
- **Loss detection is not inside CC**: it is done by `SendControl::DetectLostPackets` + the per-packet `timer_task_`, and fed to CC via `OnPacketLost`. See [`loss_recovery.md`](loss_recovery.md) §3.
- **RTT is not inside CC**: CC receives ack_delay-corrected samples via `OnRoundTripSample` and maintains an internal SRTT only as pacing input. The authoritative RTT implementation is `RttCalculator`.

---

## 8. Verification: cc_test_framework + 4 Tests

Implementation: [`test/congestion_control/`](../../../test/congestion_control/)

| File | Role |
| :--- | :--- |
| `cc_test_framework.{h,cpp}` | simulator skeleton; generates synthetic ACK / loss event streams replayed to CC |
| `network_simulator.{h,cpp}` | a simple link model (bandwidth, RTT, loss rate, buffer size) |
| `cc_algorithm_validation_test.cpp` | basic invariants of all 5 algorithms (cwnd monotonicity / recovery stability / pacing convergence) |
| `cc_bbr_detailed_test.cpp` | stage-by-stage assertions on the BBR state machines |
| `cc_comprehensive_test.cpp` | comparative runs across all algorithms |
| `cc_realistic_network_test.cpp` | realistic network-condition mixes (varying RTT / burst loss) |

**This is the entry point for verifying CC algorithms**: to change an algorithm or chase "why cwnd traces a weird curve", first reproduce a deterministic trace here, then go back to the implementation.

---

## 9. Key Invariants

| # | Invariant | Failure Symptom | Debug Hint |
|---|---|---|---|
| 1 | `bytes_in_flight_` rises in `OnPacketSent`, falls in `OnPacketAcked / OnPacketLost` | cwnd never saturates / stuck at 0 | check for missed `OnPacketLost` calls (loss path: [`loss_recovery.md`](loss_recovery.md) §3.2) |
| 2 | Recovery exit relies on `acked_packet_send_time > recovery_start_time_` | repeated cwnd-halving collapse | check that `AckEvent.acked_packet_send_time` is assigned (not 0) |
| 3 | Cubic's `epoch_start_us_ = 0` means "reset needed" | distorted post-congestion cwnd growth curve | after `OnPacketLost`, the first `IncreaseOnAck` runs `ResetEpoch` |
| 4 | BBR's `min_rtt_stamp_us_` must update on every ACK (when srtt ≤ min_rtt) | stuck in PROBE_RTT, never exits | check whether `OnPacketAcked` lines 99-103 were broken |
| 5 | The pacer's `last_update_ms_` is set to now on the first call (not 0) | the first refill floods the burst budget | the 0 sentinel in the first lines of `RefillBurstBudget` |
| 6 | `CanSend`'s cwnd check and pacer check are **in series** | sending when either cwnd or pacing is full → early overshoot | both if-layers must exist in `SendControl::SendBuffer` |

---

## 10. Related Documents

- [`packet_lifecycle.md`](packet_lifecycle.md) §6.3 — the receive-side ACK triggering strategy, upstream of CC's `OnPacketAcked`.
- [`loss_recovery.md`](loss_recovery.md) §3-§4 — loss detection and PTO, upstream of CC's `OnPacketLost`.
- [`metrics.md`](metrics.md) — export sites of `BytesInFlight` / `CongestionWindowBytes` / `PacingRateBytesPerSec` / `CongestionEventsTotal` / `SlowStartExits`.
- `test/congestion_control/cc_test_framework.{h,cpp}` — the verification framework.

---

## 11. Related RFCs

- **RFC 9002 §7** *Pluggable Congestion Control*: QUIC permits arbitrary CC; the semantic source of the event interface.
  https://datatracker.ietf.org/doc/html/rfc9002#section-7
- **RFC 9002 §B.4** NewReno for QUIC (pseudocode).
- **RFC 5681** TCP Congestion Control: the original definition of Reno.
- **RFC 9438** *CUBIC for Fast and Long-Distance Networks*: the Cubic standard.
  https://datatracker.ietf.org/doc/html/rfc9438
- **RFC 9406** *HyStart++*: Cubic's early-exit heuristic.
  https://datatracker.ietf.org/doc/html/rfc9406
- **Cardwell, Cheng, Gunn, Yeganeh, Jacobson, "BBR: Congestion-Based Congestion Control"**, ACM Queue 14(5), 2016: the original BBR v1 paper.
- **draft-cardwell-iccrg-bbr-congestion-control-02**: the IETF expression of BBR.
- **RFC 8311 §4.2** ECN-CE feedback semantics; all 5 algorithms in this repo consume `AckEvent.ecn_ce`.
- **RFC 3168** ECN in general.
