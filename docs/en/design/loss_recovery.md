# Loss Detection and Recovery

This document walks through quicX's implementation of RFC 9002 *Loss Detection and Congestion Control*: the full path of a sent packet from "being registered in the unacked table", to being wiped by an ACK, being declared lost by the time/packet thresholds, or being caught by the PTO probe fallback. After reading it you should be able to:

- Know **which class** is responsible for RTT estimation, loss declaration, and PTO, and how they chain into one data flow;
- Precisely locate the code fragment when debugging "the retransmission never went out" or "PTO keeps spinning";
- Distinguish the two kinds of "packet-level timers" in quicX — the per-packet `timer_task_` and the global `pto_timer_` — and which problem each solves.

This document only covers the **send-direction** reliability logic (loss detection + retransmission + PTO probe). The receive-side ACK triggering strategy (threshold / OoO / gap / max_ack_delay) is in [`packet_lifecycle.md`](packet_lifecycle.md) §6.3 and `recv_control.cpp::ShouldSendImmediateAck`; the congestion control algorithms themselves (Reno / Cubic / BBR) are a separate chain and are not covered here.

---

## 1. The Overall Path

The diagram below draws the four chains of an ACK-eliciting packet — from "sent" to "ACKed / declared lost / caught by PTO / retransmitted" — in one picture, with every node annotated with the **actual function name** for easy cross-reference against the source:

```text
═══════════════════════════════════════════ ① Send registration ═══════════════════════════════════════════

  BaseConnection::TrySendNew                        assemble frames + ACK, build IPacket
        │
        ▼
  BaseConnection::SendBuffer ──► sendmmsg
        │
        ▼
  SendControl::OnPacketSend(packet, len, stream_data)              ◄── key registration entry
        │
        ├── non-ACK-eliciting ── only updates largest_sent; not in unacked, not counted in cwnd
        │
        └── ACK-eliciting
              ├── congestion_control_->OnPacketSent       bytes_in_flight += len
              ├── unacked_packets_[ns][pn] = PacketTimerInfo{ send_time, len, task, ... }
              ├── per-packet timer_task_                  fallback loss marking after PTO
              └── reset the global pto_timer_                     = PTO_with_backoff()


═══════════════════════════════════════════ ② Feedback path ═══════════════════════════════════════════

  peer ACK arrives
        │
        ▼
  FrameProcessor::HandleFrames ──► SendManager::OnPacketAck ──► SendControl::OnPacketAck
        │
        ├── rtt_calculator_.UpdateRtt(send_time, now, ack_delay)              【§2 RTT】
        │
        ├── iterate ACK ranges, for every ACKed pn:
        │     ├── timer_->RemoveTimer(per-packet timer_task_)
        │     ├── congestion_control_->OnPacketAcked
        │     ├── stream_data_ack_cb_(stream_id, offset, len, fin)            notify SendStream
        │     └── unacked_packets_[ns].erase(pn)
        │
        ├── DetectLostPackets(now, ns, largest_acked)                         【§3 loss detection】
        │     for each unacked pn < largest_acked:
        │         if (largest_acked >= pn + 3)            → packet threshold
        │         else if (time_since_sent > 9/8·SRTT)    → time threshold
        │         → move to lost_packets_, call packet_lost_cb_
        │
        ├── rtt_calculator_.OnPacketAcked()                                   reset PTO backoff
        │
        └── reschedule pto_timer_                                             【§4 PTO scheduling】
              ├── has ack-eliciting in-flight   → reset PTO (with current backoff)
              ├── else if !handshake_complete_  → still schedule (handshake probe)
              └── otherwise                     → keep cancelled


═══════════════════════════════════════════ ③ Fallback path ═══════════════════════════════════════════

  pto_timer_ expires
        │
        ▼
  SendControl::OnPTOTimer                                                     【§4 PTO fire】
        │
        ├── rtt_calculator_.OnPTOExpired()                pto_count_++
        ├── pick the earliest unacked, mark lost ─► packet_lost_cb_ ─► ActiveSend
        ├── handshake phase with nothing to retransmit: probe_needed_cb_            → inject PING (Initial/Handshake)
        ├── after handshake, always:            application_probe_cb_      → inject PING (1-RTT)
        └── reschedule pto_timer_                           = PTO × 2^pto_count_


═══════════════════════════════════════════ ④ Retransmission execution ═══════════════════════════════════════════

  ActiveSend ──► worker ──► BaseConnection::TrySend
        │
        ├── NeedReSend()  → TrySendRetransmit                                 【§5 retransmission】
        │     ├── take lost_packets_.front()
        │     ├── allocate a new PN + current KeyPhase
        │     ├── re-encode + AEAD encrypt
        │     ├── OnPacketSend(new_pn, old_stream_data)   ◄── back to registration in ①
        │     └── SendBuffer
        │
        └── otherwise TrySendNew
```

---

## 2. RTT Estimation: `RttCalculator`

Implementation: [`src/quic/connection/controller/rtt_calculator.h`](../../../src/quic/connection/controller/rtt_calculator.h) / [`rtt_calculator.cpp`](../../../src/quic/connection/controller/rtt_calculator.cpp) (~130 lines, logic maps one-to-one to RFC 9002 §5).

### 2.1 The Three Estimators

| Field | Meaning | Update Formula (RFC 9002 §5) |
| :--- | :--- | :--- |
| `latest_rtt_` | The most recent sample (now − send_time) | Direct assignment |
| `min_rtt_` | The connection's historical minimum RTT | `min(min_rtt_, latest_rtt)` |
| `smoothed_rtt_` | Smoothed RTT, used by PTO / time threshold | `smoothed_rtt = 7/8·smoothed_rtt + 1/8·adjusted_rtt` |
| `rtt_var_` | RTT variance | `rttvar = 3/4·rttvar + 1/4·\|smoothed_rtt − adjusted_rtt\|` |

`adjusted_rtt = latest_rtt − ack_delay`, and the ACK Delay is deducted **only when** `latest_rtt ≥ min_rtt + ack_delay` (to avoid subtracting into a negative number and polluting the sample). The code is right in `UpdateRtt()`:

```cpp
uint32_t adjusted_rtt = latest_rtt_;
if (latest_rtt_ >= (min_rtt_ + ack_delay)) {
    adjusted_rtt -= ack_delay;
}
```

> Known divergence: RFC 9002 §5.3 requires ignoring the peer's `max_ack_delay` before handshake_confirmed and taking `min(ack_delay, peer_max_ack_delay)` afterwards. The code does not yet have the handshake_confirmed signal (registered in roadmap §2), but `SendControl::GetEffectiveMaxAckDelay()` already provides equivalent protection on the PTO computation path — before the handshake it is always treated as 0 (see §4.1).

### 2.2 Initial RTT and the P3 Knob

Before the first ACK arrives, `smoothed_rtt_` must have an initial value, otherwise PTO computes to 0. RFC 9002 §6.2.2 recommends 333 ms; quicX uses 250 ms (`kInitRttDefaultMs`), making the cold-start PTO ≈ 250 + 4·125 + 25 ≈ 775 ms — the same order of magnitude as the RFC baseline but slightly more aggressive, friendlier to typical internet RTTs.

To keep loopback benchmarks from being dragged down by the ~775 ms cold-start PTO, a process-level override was introduced:

| Interface | Behavior |
| :--- | :--- |
| `GetDefaultInitialRtt()` | Called by `RttCalculator::Reset()` at construction / reset, returns the current override |
| `SetDefaultInitialRtt(ms)` | Process-level lowering (benchmarks/unit tests only); passing 0 or >250 falls back to the default |

This is the P3 knob of `docs/internal/perf_e2e_analysis.md` §6. Do **not** lower it in production code — an initial PTO of 50 ms on loopback triggers spurious retransmissions on the second hop of a transcontinental link.

### 2.3 The PTO Formula and Backoff

```cpp
// rtt_calculator.cpp:94
uint32_t GetPT0Interval(uint32_t max_ack_delay) {
    // PTO = SRTT + max(4·RTTVAR, kGranularity) + max_ack_delay
    return smoothed_rtt_ + std::max<uint32_t>(rtt_var_ << 2, 1) + max_ack_delay;
}

uint32_t GetPTOWithBackoff(uint32_t max_ack_delay) {
    // PTO * 2^pto_count_, pto_count_ capped at kMaxPTOBackoff = 6 (i.e. 64×)
    return GetPT0Interval(max_ack_delay) << std::min(pto_count_, kMaxPTOBackoff);
}
```

Backoff state is driven by two call sites:

| Event | Effect |
| :--- | :--- |
| `OnPTOExpired()` (once per PTO expiry) | `pto_count_++` (capped at 6), `consecutive_pto_count_++` |
| `OnPacketAcked()` (any ACK) | Both reset to zero |

`consecutive_pto_count_` exists for the connection-idle close — `kMaxConsecutivePTOs = 16`, about 3 PTO cycles, making it easier to tell "the path is dead" from "network jitter".

---

## 3. Loss Detection: `SendControl::DetectLostPackets`

Implementation: [`src/quic/connection/controller/send_control.cpp:556`](../../../src/quic/connection/controller/send_control.cpp). After each ACK frame, `OnPacketAck` calls it once after processing all ranges, scanning the unconfirmed packets with `pn < largest_acked` in the same packet number space against the two rulers of RFC 9002 §6.1:

### 3.1 Packet Threshold (RFC 9002 §6.1.1)

```cpp
// send_control.h
static constexpr uint32_t kPacketThreshold = 3;
// send_control.cpp:581
if (largest_acked >= pkt_num + kPacketThreshold) {
    should_declare_lost = true;
}
```

This defines "at least 3 PNs behind largest_acked and still unaccounted for → lost"; 3 is the RFC-recommended value and also the lower bound of reordering tolerance.

### 3.2 Time Threshold (RFC 9002 §6.1.2)

```cpp
// send_control.h
static constexpr uint32_t kTimeThresholdNum = 9;   // 9/8 · SRTT
static constexpr uint32_t kTimeThresholdDen = 8;
// send_control.cpp:558
uint64_t loss_delay = (rtt_calculator_.GetSmoothedRtt() * 9) / 8;
loss_delay = std::max(loss_delay, uint64_t(1));   // at least 1 ms
```

The reference point is `largest_acked`'s own send_time (not now):

```cpp
uint64_t time_since_sent =
    largest_acked_send_time > info.send_time_
        ? largest_acked_send_time - info.send_time_
        : 0;
if (time_since_sent > loss_delay) {
    should_declare_lost = true;
}
```

Why use `largest_acked`'s send time instead of the current time? Because we want to rule out "the packet is still in flight". If `pn` was sent more than 9/8·SRTT earlier than `largest_acked`, and `largest_acked` has already been ACKed, then `pn` is most likely not merely late — it is genuinely lost.

### 3.3 Side Effects of Marking Lost

Whenever either threshold is met:

1. Cancel the per-packet timer_task_ (to keep it from firing again and double-processing);
2. Remove from `unacked_packets_[ns]` (anti-leak; the retransmit path re-registers the entry under a new PN);
3. Push onto the `lost_packets_` queue, carrying `stream_data` along;
4. `congestion_control_->OnPacketLost(...)` notifies the CC layer to reduce the window (the exact action is decided by Reno/Cubic/BBR each);
5. `packet_lost_cb_(packet)`: the connection layer's `SendManager` takes this callback to uniformly call `send_retry_cb_` → `BaseConnection::ActiveSend`, ensuring the worker enters the send loop on the next round.

> **Key invariant**: a packet marked lost is **not** "resent" on its original PN. QUIC forbids PN reuse (RFC 9000 §13.2.3). Resending means constructing a new packet with **a new PN + the same payload** — see §5.

### 3.4 The Per-Packet Fallback Timer

Note that `OnPacketSend` also attaches a separate `timer_task_` to **every** ack-eliciting packet, which expires after one PTO duration and independently re-runs the lost check:

```cpp
// send_control.cpp:102
auto timer_task = common::TimerTask([this, pkt_len, packet, ns] {
    auto it = unacked_packets_[ns].find(packet->GetPacketNumber());
    if (it == unacked_packets_[ns].end() || it->second.is_lost) {
        return;  // DetectLostPackets already handled it
    }
    it->second.is_lost = true;
    lost_packets_.push_back(LostPacketEntry{packet, it->second.stream_data});
    ...
});
```

It and `DetectLostPackets` are in a **redundant fallback** relationship:

- When an ACK arrives, `DetectLostPackets` judges loss via the packet/time thresholds — the fast path;
- If no ACK arrives for a long time and that path never triggers, the per-packet timer fires by itself after one PTO duration — the slow path.

The `is_lost` field is the deduplication lock between the two paths: whoever arrives first sets the flag; the later one sees it and returns idle, avoiding a double decrement underflow of `bytes_in_flight` (a bug fixed once under P0-1, noted in the comments).

---

## 4. PTO Probing: `pto_timer_` + `OnPTOTimer`

PTO (Probe Timeout) is not the same thing as the per-packet timer above. It is the **globally unique** "silence detector" — as long as there is ack-eliciting data in flight, one timer is kept; its expiry does not directly declare anyone lost, but instead triggers **active probing**.

### 4.1 Scheduling Rules (RFC 9002 §6.2.1)

`pto_timer_` is (re)scheduled in three places:

| Scheduling Point | Condition | Note |
| :--- | :--- | :--- |
| End of `OnPacketSend` | Reset for every ack-eliciting packet sent | Uses `GetPTOWithBackoff(GetEffectiveMaxAckDelay())` |
| End of `OnPacketAck` | Still ack-eliciting in-flight: reset; else handshake phase: keep; else: cancel | See the Bug #18 note below |
| End of `OnPTOTimer` | Regardless of whether this round retransmitted, reschedule with the new backoff | `pto_count_` was already incremented by `OnPTOExpired()` |

`GetEffectiveMaxAckDelay()` is mandated by RFC 9002 §6.2.1: before the handshake is confirmed, the peer's max_ack_delay must be treated as 0, otherwise PTO gets inflated by a transport parameter the peer has not yet reliably received, causing spurious stalls during the handshake.

### 4.2 Bug #18: The Cost of a Wrong Branch

The PTO rescheduling at the end of `OnPacketAck` has a lesson behind it. The original logic was "reset PTO only during the handshake"; the result was that after the handshake, if an ACK only partially covered in-flight packets (typical scenario: a selective ACK leaves one old retransmission unconfirmed), PTO got permanently cancelled; subsequently no ACK ever arrived and no new packet went out (FC stuck), and the connection sat all the way to idle timeout.

The fixed logic converges to:

```cpp
// send_control.cpp:443
bool has_ack_eliciting_in_flight = false;
for (int s = 0; s < PacketNumberSpace::kNumberSpaceCount; s++) {
    if (!unacked_packets_[s].empty()) {
        has_ack_eliciting_in_flight = true; break;
    }
}
if (has_ack_eliciting_in_flight) {
    // reset with the just-zeroed pto_count_ → a PTO without backoff
} else if (!handshake_complete_) {
    // during the handshake, keep PTO alive even when in-flight is empty
    // (guards against server anti-amplification throttling)
} else {
    // after handshake with everything cleared: cancel, per §6.2.1
}
```

`unacked_packets_[]` holds only ack-eliciting packets (`OnPacketSend` early-returns ACK-only at the top), so "empty" equals "no ack-eliciting in flight".

### 4.3 What Happens When PTO Fires (RFC 9002 §6.2.4)

The core actions of `OnPTOTimer` come in three parts:

```cpp
rtt_calculator_.OnPTOExpired();   // backoff +1

// 1) pick the earliest unacked → mark lost → take the retransmit path
for (int ns = 0; ns < kNumberSpaceCount; ns++) {
    if (!unacked_packets_[ns].empty()) {
        auto it = unacked_packets_[ns].begin();   // the earliest
        if (it->second.packet && !it->second.is_lost) {
            it->second.is_lost = true;
            lost_packets_.push_back({...});
            congestion_control_->OnPacketLost({...});
            packet_lost_cb_(it->second.packet);
            found_retransmit = true; break;
        }
    }
}

// 2) handshake phase with nothing to retransmit → have the connection inject a PING (Initial/Handshake)
if (!found_retransmit && !handshake_complete_ && probe_needed_cb_) {
    probe_needed_cb_();
}

// 3) after the handshake, always inject a 1-RTT PING (Bug-19 fix)
if (handshake_complete_ && application_probe_cb_) {
    application_probe_cb_();   // sent even if step 1 succeeded
}

// reschedule pto_timer_ = PTO × 2^pto_count_
```

Note the "unconditional PING" in step 3 — the engineering fulfillment of §6.2.4's *probe MUST be ack-eliciting*: if the path's loss rate is high enough to swallow retransmissions too, the original retransmission never reaches the peer and loss detection on this end cannot advance. A 22-byte PING packet is not blocked by stream FC, giving PTO probing an **independent** chance of success, breaking the transfer-5MB / quicx-quic-go style deadlock of "endlessly retransmitting the lost retransmission".

The two callbacks are wired in `BaseConnection`'s constructor:

```cpp
// connection_base.cpp:81
send_manager_.GetSendControl().SetProbeNeededCallback([this]() {
    auto ping = std::make_shared<PingFrame>();
    ToSendFrame(ping);
});
send_manager_.GetSendControl().SetApplicationProbeCallback([this]() {
    auto ping = std::make_shared<PingFrame>();
    ToSendFrame(ping);
    ActiveSend();   // nudge the worker into the send loop on the same thread,
                    // otherwise the PING merely sits in wait_frame_list_
});
```

---

## 5. Retransmission Execution: `BaseConnection::TrySendRetransmit`

Implementation: [`src/quic/connection/connection_base.cpp:1658`](../../../src/quic/connection/connection_base.cpp). It and `TrySendNew` are dispatched by `BaseConnection::TrySend` — when `send_control.NeedReSend()` is true (i.e. `lost_packets_` is non-empty), the retransmit branch is taken.

### 5.1 RFC 9000 §13.3: Retransmission = New PN + Old Payload

```cpp
auto lost_entry = lost_packets.front();    // carries packet + stream_data
lost_packets.pop_front();
auto lost_pkt = lost_entry.packet;

// 1) fetch the cryptographer of the current encryption level
//    (keys may already be discarded → simply give up)
auto cryptographer = connection_crypto_.GetCryptographer(lost_pkt->GetCryptoLevel());
if (!cryptographer) return !lost_packets.empty();   // try the next one

// 2) cwnd check; not enough → push back to the queue head
if (send_manager_.GetAvailableWindow() == 0) {
    lost_packets.push_front(lost_entry);
    send_manager_.SetCwndLimited();
    return false;
}

// 3) allocate a new PN
uint64_t new_pn = send_manager_.GetPacketNumber().NextPacketNumber(ns);
lost_pkt->SetPacketNumber(new_pn);
lost_pkt->GetHeader()->SetPacketNumberLength(PacketNumber::GetPacketNumberLength(new_pn));
lost_pkt->SetCryptographer(cryptographer);

// 4) RFC 9001 §6.5: retransmission must be re-encrypted with the *current* KeyPhase
//    (Short header only)
if (lost_pkt->GetHeader()->GetHeaderType() == PacketHeaderType::kShortHeader) {
    lost_pkt->GetHeader()->GetShortHeaderFlag().SetKeyPhase(connection_crypto_.GetCurrentKeyPhase());
}

// 5) re-encode + AEAD
auto buffer = std::make_shared<common::SingleBlockBuffer>(...);
if (!lost_pkt->Encode(buffer)) return false;

// 6) key point: register into unacked_packets_ with the *new PN* but the *original stream_data*
//    so that when the new PN is ACKed next time, stream_data_ack_cb_ can feed
//    the byte range back to SendStream
send_control.OnPacketSend(now, lost_pkt, encoded_size, lost_entry.stream_data);

// 7) send it out
return SendBuffer(buffer);
```

### 5.2 Several Easy-to-Get-Wrong Points

1. **stream_data must migrate with the packet**. `LostPacketEntry { packet, stream_data }` carries the original frame's byte range along, and the new-PN registration passes it back untouched into `unacked_packets_[ns][new_pn]`. If this step is lost, when that retransmitted packet is later ACKed, `stream_data_ack_cb_` knows nothing, and `SendStream`'s selective-ACK bookkeeping is forever missing a segment — this was the root cause of the early quicx 5 MB file-transfer hang.
2. **The current KeyPhase must be used**. Long headers (Initial/Handshake/0-RTT) have no KeyPhase field and skip this; for Short headers, reusing the old phase stored in the packet object instead of the current one makes the peer's AEAD validation fail and the packet get silently dropped — the loss detector loops forever, PN explodes past 130k, and the connection eventually hits idle timeout.
3. **On insufficient cwnd, push_front**. Put the already-popped entry back at the queue head; otherwise the next `TrySendRetransmit` sees a newer entry that should not be sent first.

### 5.3 `DiscardPacketNumberSpace`: Cleanup After Handshake Completion

[`send_control.cpp:508`](../../../src/quic/connection/controller/send_control.cpp). When Initial/Handshake keys are dropped (RFC 9000 §4.10), the unacked / lost packets in those PN spaces lose any chance of retransmission (the peer can no longer decrypt them):

```cpp
void SendControl::DiscardPacketNumberSpace(PacketNumberSpace ns) {
    for (auto& pair : unacked_packets_[ns]) timer_->RemoveTimer(pair.second.timer_task_);
    unacked_packets_[ns].clear();
    for (auto it = lost_packets_.begin(); it != lost_packets_.end(); ) {
        if (...same ns...) it = lost_packets_.erase(it); else ++it;
    }
    pkt_num_largest_sent_[ns] = 0;
    pkt_num_largest_acked_[ns] = 0;
    largest_sent_time_[ns] = 0;
}
```

It is called once each at `connection_client.cpp:307` and `connection_server.cpp:168`, corresponding to the client / server Initial+Handshake key drop after 1-RTT is ready. At the same moment `RecvControl::DiscardPacketNumberSpace` is also called, symmetrically cleaning up the receive-side ACK state.

---

## 6. Semantic Comparison of the Three "Packet-Level Timers"

quicX has three loss-recovery-related timers that are easy to confuse; here they are side by side:

| Name | Scope | Duration | On Expiry | Cancellation |
| :--- | :--- | :--- | :--- | :--- |
| per-packet `timer_task_` (one per unacked packet) | Single packet | `GetPTOWithBackoff()` | Mark this packet lost; CC reduces cwnd; trigger retransmit callback | When the packet is ACKed |
| global `pto_timer_` (one per connection) | Across PN spaces | `GetPTOWithBackoff()` | Mark the earliest unacked lost + inject PING | When an ACK arrives (reset or cancel) |
| `ack_timer_` (receive side, see `recv_control.h`) | Single PN space | `max_ack_delay_` (25 ms default) | Force the accumulated ACK out immediately | Once an ACK is sent |

The three address "silence" in **different dimensions**:

- per-packet: "no ACK for a long time" at the single-packet level;
- pto_timer: "is the peer dead" at the connection level;
- ack_timer: this end's "time to reply with an ACK, stop accumulating" — symmetric to this document's main line, not part of loss detection.

---

## 7. Key Invariants

A few invariants that span the whole chain, for easier debugging:

1. **`unacked_packets_[ns]` holds only ack-eliciting packets**. `OnPacketSend` early-returns at line 69, keeping ACK-only packets out of the table. This invariant is implicitly relied upon by the "does PTO need in-flight?" check at the end of `OnPacketAck`; violating it makes PTO spin with no ack-eliciting data.
2. **`is_lost` is the deduplication lock between the per-packet path and DetectLostPackets**. Both paths check before set, guaranteeing the CC layer's `OnPacketLost` is called at most once per PN (avoiding bytes_in_flight underflow).
3. **A retransmitted PN must re-register stream_data**. `TrySendRetransmit::OnPacketSend(new_pn, lost_entry.stream_data)` is the sole entry of this chain; missing it means the retransmit's ACK cannot flow back to SendStream.
4. **`pto_count_` is maintained solely by PTO**. The per-packet timer_task_ does not call `OnPTOExpired()` (explicitly stated in the comment at line 103) — one widespread loss event may fire 6 packet timers simultaneously; if each incremented the backoff, `pto_count_` would instantly reach 6, and the next real PTO would be wrongly stretched 64×.
5. **max_ack_delay on the PTO computation path must go through `GetEffectiveMaxAckDelay()`**. All four call sites (OnPacketSend per-packet timer / OnPacketSend pto_timer_ rearm / OnPacketAck rearm / OnPTOTimer rearm) go through this accessor, which returns 0 before handshake completion, per §6.2.1.
6. **No lost / unacked residue remains in a discarded PN space**. `DiscardPacketNumberSpace` is the single cleanup entry; skipping it makes PTO retransmit endlessly in a PN space whose keys are already abandoned.

---

## 8. Related Documents

- [`packet_lifecycle.md`](packet_lifecycle.md) §6.3 — the receive-side ACK triggering and frame dispatch, the mirror image of this document; how quickly `RecvControl::ShouldSendImmediateAck` replies with ACKs directly affects the density of RTT samples here.
- [`handshake_state_machine.md`](handshake_state_machine.md) — explains when `handshake_complete_` becomes true and on which state edge `DiscardPacketNumberSpace` fires.
- `docs/internal/perf_e2e_analysis.md` §6 — the origin and applicability boundary of the P3 knob (`SetDefaultInitialRtt`).
- `src/quic/congestion_control/` — the receivers of `OnPacketSent / OnPacketAcked / OnPacketLost`; the CC algorithms themselves are intentionally out of scope here.

---

## 9. Related RFCs

- [RFC 9002 §5](https://www.rfc-editor.org/rfc/rfc9002.html#name-estimating-the-round-trip-t) — Estimating the Round-Trip Time (`RttCalculator::UpdateRtt`)
- [RFC 9002 §5.3](https://www.rfc-editor.org/rfc/rfc9002.html#name-estimating-smoothed_rtt-and) — the `min(ack_delay, peer_max_ack_delay)` rule (known divergence, registered in roadmap §2)
- [RFC 9002 §6.1.1](https://www.rfc-editor.org/rfc/rfc9002.html#name-packet-threshold) — Packet Threshold (`kPacketThreshold = 3`)
- [RFC 9002 §6.1.2](https://www.rfc-editor.org/rfc/rfc9002.html#name-time-threshold) — Time Threshold (`9/8 · SRTT`)
- [RFC 9002 §6.2.1](https://www.rfc-editor.org/rfc/rfc9002.html#name-computing-pto) — Computing PTO (`GetPT0Interval` / `GetEffectiveMaxAckDelay`)
- [RFC 9002 §6.2.2.1](https://www.rfc-editor.org/rfc/rfc9002.html#name-before-address-validation) — handshake-phase PTO probing (`probe_needed_cb_`)
- [RFC 9002 §6.2.4](https://www.rfc-editor.org/rfc/rfc9002.html#name-sending-probe-packets) — PTO probe must be ack-eliciting (`application_probe_cb_` / Bug-19 PING)
- [RFC 9000 §13.3](https://www.rfc-editor.org/rfc/rfc9000.html#name-retransmission-of-information) — Retransmission semantics (retransmission = new PN + old frames)
- [RFC 9000 §4.10](https://www.rfc-editor.org/rfc/rfc9000.html#name-packet-number-encoding-and-) / [RFC 9001 §4.9](https://www.rfc-editor.org/rfc/rfc9001.html#name-discarding-unused-keys) — Discard packet number spaces (cleanup after key drop)
- [RFC 9001 §6.5](https://www.rfc-editor.org/rfc/rfc9001.html#name-receiving-with-different-ke) — retransmission must be re-encrypted with the current KeyPhase
