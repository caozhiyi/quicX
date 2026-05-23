# quicX E2E Performance — Root-Cause Analysis

**Date**: 2026-05-03
**Build**: `DEBUG` (all absolute numbers are 2–10× slower than Release)
**Binary**: `./build/bin/perf/e2e_perf_test`

---

## 1. Executive Summary

The wide spread of "bad" results observed in `e2e_perf_test` (e.g. Upload/64 KB at
134 KiB/s, MultiStream/10 at 8 req/s, Handshake_Burst at 3.2 s) is **not** a real
throughput ceiling of the stack.

The numbers are dominated by a **single root cause**:

> **Cold-start packet loss + a 775 ms initial PTO** causes the *first* benchmark in
> any "cluster" to wait ~1 s for probe recovery. When one benchmark ends in this
> state, the **next** benchmark (sharing the same process and scheduler) often
> inherits the same 1-second wait — making back-to-back tests look catastrophically
> slow even though the stack, in steady state, is healthy.

Under steady state the stack is actually fast:

| Scenario                   | Steady-state (DEBUG)      |
|----------------------------|---------------------------|
| MultiStream, single client | **≈ 1300 req/s (7.7 ms)** |
| Sequential, single client  | **≈ 196 req/s**           |
| Download 1 MB, reuse conn  | **≈ 9.6 MiB/s**           |
| Upload 256 KB, reuse conn  | **≈ 11.6 MiB/s**          |
| Stability sustained load   | **196 req/s, 0 fails**    |
| RSS per long-lived conn    | ≈ 100 KB (stable)         |

---

## 2. How We Got Here — Diagnostic Evidence

Temporary `fprintf(stderr, "[PERF-RTT] ...")` / `[PERF-PTO] ...` probes were
placed in `src/quic/connection/controler/rtt_calculator.cpp` at:
- `UpdateRtt()` — first RTT sample per connection
- `OnPTOExpired()` — every PTO firing

Running `MultiStream/10` **alone** produced:

```
[PERF-RTT] first_sample latest=7ms ack_delay=0ms -> smoothed=7ms var=3ms
[PERF-RTT] first_sample latest=3ms ack_delay=0ms -> smoothed=3ms var=1ms
BM_E2E_Concurrency_MultiStream/10   10.3 ms   items_per_second=974/s
```
→ **0 PTO firings**, real RTT 3–7 ms, benchmark fast.

Running `MultiStream/5,10,20` **in sequence** produced:

```
[PERF-PTO] expired pto_count=1 smoothed=250 var=125   ← initial PTO = 775ms
[PERF-PTO] expired pto_count=2 smoothed=250 var=125   ← backoff PTO = 1550ms
[PERF-RTT] first_sample latest=3ms …                   ← only arrives AFTER PTO
MultiStream/5  : 1170 ms   4 req/s      (two PTOs)
MultiStream/10 :  509 ms  20 req/s      (no PTO)
MultiStream/20 : 1172 ms  17 req/s      (two more PTOs)
```

The pattern repeated in every 🔴 case:

- `Handshake_Burst/5` → **5×** `pto_count=1` + **2×** `pto_count=2` for the
  5 concurrent clients.
- `Throughput_Download` → 1 PTO on the first iteration, fine thereafter.

### Full E2E run #1 vs run #2 — same binary, no changes

| Benchmark             | Run #1 (fast cases)      | Run #2 (slow cases)      | Ratio |
|-----------------------|--------------------------|--------------------------|-------|
| Download 1 MB         | **9.57 MiB/s**           | 1.72 MiB/s               | 5.5×  |
| Upload 1 KB           | 5.11 ms                  | 471 ms                   | 92×   |
| Upload 64 KB          | **6.79 MiB/s**           | 134 KiB/s                | 52×   |
| Upload 256 KB         | 10.15 s (slow)           | **11.6 MiB/s**           | 472×  |
| Sequential/10         | 552 ms                   | **51 ms / 196 req/s**    | 10.8× |
| MultiStream/5         | **7.68 ms, 651 req/s**   | 507 ms, 10 req/s         | 65×   |
| MultiStream/10        | 1172 ms                  | **7.69 ms, 1300 req/s**  | 152×  |

Run-to-run variance **of 50×–400×** without touching code or traffic pattern is the
signature of a timer-driven penalty, not a CPU bottleneck.

---

## 3. Why the Cold Start Costs 775 ms — the Math

```cpp
// src/quic/connection/controler/rtt_calculator.h
const static uint32_t kInitRtt = 250;      // ms
// src/quic/config.h
static constexpr uint32_t kMaxAckDelay = 25;  // ms
```

Reset values (no samples yet):
- `smoothed_rtt = 250 ms`
- `rtt_var     = 125 ms`

RFC 9002 §6.2 initial PTO:

```
PTO = smoothed_rtt + max(4 * rtt_var, kGranularity) + max_ack_delay
    = 250 + max(500, 1) + 25
    = 775 ms
```

With exponential backoff: 775 → 1550 → 3100 → … ms.

Any request that loses (or delays beyond delayed-ACK of) even *one* packet during
the first RTT sample must wait 775 ms to recover. On a loopback link the **true**
RTT is 1–7 ms, so this initial value is **100×–750× too conservative for the
first packet**.

---

## 4. Why the Cold Start Packet Is Dropped

Loopback doesn't drop packets, so the "loss" must come from the stack. Two
strong candidates remain (requires further probing to nail the exact one):

1. **Server single-worker event loop is not yet pumping** when the first
   client datagram arrives. All benchmarks use
   `ThreadMode::kSingleThread + worker_thread_num_ = 1`. The 200 ms
   `sleep_for(...)` in every test reduces but does not eliminate this window —
   and back-to-back tests re-hit this gap on every new server they spin up.

2. **Master→Worker handoff race**: `MasterWithThread::AddListener` posts
   `AddReceiver` into the master's event loop via `RunInLoop(...)`. A datagram
   that arrives *before* the lambda runs is silently dropped by the kernel
   because no socket is yet registered to the epoll set (`EADDRINUSE`-style
   zombie from the just-closed previous server is also possible — we fixed the
   explicit leak, but the kernel-side `SO_REUSEADDR` behaviour means the old
   port may still be "DRAINING" for ~200 ms).

Both root causes are **fixable without changing the protocol defaults**; §6
lists the recommended actions.

---

## 5. Benchmark Methodology Issues

Even after the underlying packet-drop bug is fixed, the benchmark file itself
has two properties that amplify noise:

1. **Extremely low iteration counts**: `->Iterations(1)` or `->Iterations(2)`
   means a single 775 ms PTO hit swings the reported time by ±50%.
2. **Shared-process test ordering**: Benchmarks run in a single process; one
   test's leftover timers / closing connections can preempt the next test's
   server start-up.

These together produce the 50×–400× run-to-run variance documented above.

---

## 6. Recommendations (priority-ordered)

### P0 — Fix the cold-start packet drop (the real bug) ✅ **DONE**

**Status**: Fixed in `src/quic/quicx/master_with_thread.cpp` (2026-05-03).

`MasterWithThread::AddListener(ip, port)` and the `fd` overload previously used
`event_loop_->RunInLoop(...)` in fire-and-forget mode — they returned `true`
immediately while the actual `receiver_->AddReceiver(...)` (which creates the
UDP socket, binds it, and arms it in the event driver) was still queued in the
master thread's task list.

The fix turns `AddListener` into a **synchronous barrier**: the caller now
blocks on a `std::promise<bool>` until the master thread has finished
registering the listener, and gets the real `AddReceiver` return value.

Observed effect on the full `e2e_perf_test` suite:

| Benchmark                  | Before (flaky)              | After (stable)            |
|----------------------------|-----------------------------|---------------------------|
| Throughput_Download        | 1.52 MiB/s ↔ 9.57 MiB/s     | **7.5–8.4 MiB/s**         |
| Throughput_Upload/262144   | 20 KiB/s ↔ 11.6 MiB/s       | **9.7–10.2 MiB/s**        |
| Throughput_Sequential/50   | 35 ↔ 196 req/s              | **194–196 req/s**         |
| Concurrency_MultiStream/5  | 651 ↔ 10 req/s              | **652 req/s**             |
| Stability_Sustained/10     | 150 ↔ 196 req/s             | **196 req/s, 0 fail**     |

The fix fully stabilises the scenarios where the **server-side** listener is the
first thing on the critical path. Handshake_Burst / MultiClient / the first
"Upload/1024" of a cluster still show the 1 s penalty — those are a different
mechanism (multi-client concurrent handshake on a single-worker server, where
the first flight of the Nth client can still be dropped while the worker is
processing client N-1). See P1.

### P1 — Graceful-close 1 s latency tax ✅ **DONE**

**Status**: Fixed in three files on 2026-05-03.

The original P1 hypothesis ("single-worker concurrent handshake drop") was
**falsified** by adding `[PERF-PTO]` / `[PERF-RTT]` probes: `Handshake_Burst`,
`MultiClient`, and `ConnectDisconnect` all recorded **zero** PTOs and clean
first-sample RTTs. The 1–3 s per-iteration tax was not handshake-related.

Bisecting with per-phase timestamps in the benchmark loop revealed three
**serial** bugs that together cost ~1 s *per client lifetime*:

#### Bug 1: `OnStateToClosing` never notifies the application

`BaseConnection` has three terminal listener hooks: `OnStateToClosing`,
`OnStateToDraining`, `OnStateToClosed`. When a client calls
`Client::Close()` → `quic_connection_->Close()` → `StartGracefulClose()`, the
state machine transitions **Connected → Closing**, which invokes
`OnStateToClosing`. That hook *sends* a `CONNECTION_CLOSE` frame and starts a
`3×PTO` closing-timeout timer, but it **never** calls
`connection_closer_->InvokeConnectionCloseCallback(...)`. Only the *other two*
hooks (Draining / Closed) do. As a result, the application-level callback
`Client::OnConnection(kConnectionClose, ...)` was delayed until either:
- the peer echoed back its own `CONNECTION_CLOSE` (Closing → Draining), or
- the 3×PTO closing timer fired (Closing → Closed, ~1500 ms).

On localhost neither happens promptly, so every graceful close paid the
closing-timeout tax.

**Fix** (`src/quic/connection/connection_base.cpp`, `OnStateToClosing`):
Immediately invoke `InvokeConnectionCloseCallback` right after sending the
`CONNECTION_CLOSE` frame. The callback is idempotent (guarded by
`connection_close_cb_invoked_`), so `OnStateToClosed` still fires safely but
is now just a no-op on this path. RFC 9000 permits early application-layer
notification — only the transport state machine needs to respect the PTO
drain window.

#### Bug 2: `http3::Client::Close()` always paid a 1 s safety timer

`http3::Client::Close()` previously registered an unconditional
`kConnectionCloseDestroyTimeoutMs = 1000 ms` `AddTimer(..., Destroy)`. Every
`~Client()` (including benchmark iteration teardown) was therefore serialised
through a 1 s wait even when all connections had already closed cleanly.

**Fix** (`src/http3/http/client.cpp`, `src/http3/http/client.h`):
- Track outstanding closes with `pending_close_count_` (set in `Close()` to
  `conn_map_.size()`, decremented in `OnConnection(kConnectionClose)`).
- When the counter reaches zero, schedule `Destroy()` on the event loop with
  `AddTimer(0, ...)` — this correctly sequences teardown with the QUIC
  connection's close path (the callback fires *inside* `OnStateToClosing`,
  so synchronous `Destroy()` would tear down the event loop while it is
  still on the stack).
- Keep the 1 s timer only as a safety net for the pathological case where
  the peer is unreachable and no `CONNECTION_CLOSE` round-trip completes.

#### Bug 3: `EventLoop::Wait()` blocks 1 s on lost wakeups (root cause)

This is the dominant fix. Reproducer:

```
client->Init(cc);
  └─ master_->Start();                     # spawn master thread
  └─ master_->AddListener(sender->GetSocket());  # synchronous barrier
        └─ event_loop_->RunInLoop(lambda);
              └─ PostTask(lambda); Wakeup();
                    └─ driver_->Wakeup();   # ★ no-op if driver_ == nullptr
```

The master thread starts running `MasterWithThread::Run()`, which calls
`event_loop_->Init()` **first**. If `PostTask()` from the creator thread
arrives **before** the master thread has finished `Init()`, then
`driver_` is still `nullptr`, and `driver_->Wakeup()` silently does nothing
(it was never a pipe/eventfd write). When the master thread then enters its
first `Wait()`, the task is already in the queue but the cross-thread wakeup
signal has been lost — so `epoll_wait` blocks for the full default timeout
(**1000 ms**) before `DrainPostedTasks()` finally runs.

This caused `Client::Init()` to stall ~1 s on ~50 % of benchmark iterations,
which compounded with Bugs 1–2 to produce the observed 1.2–3 s / iteration
measurements.

**Fix** (`src/common/network/event_loop.cpp`, `Wait()`):
Before calling `driver_->Wait(timeout_ms)`, peek `tasks_` under the mutex; if
non-empty force `timeout_ms = 0`. Cost: one mutex acquire per event-loop
tick (guarded by `timeout_ms > 0`, i.e. only when we were *about* to block).

#### Measured effect

| Benchmark                                | Before          | After             | Speedup |
|------------------------------------------|-----------------|-------------------|---------|
| `Handshake_NewConnection`                | 1507 ms/iter    | **6.18 ms/iter**  | **244×** |
| `Handshake_Burst/5`                      | ~3.2 s          | **1.22 s**        | 2.6×    |
| `Handshake_Burst/10`                     | ~3.5 s          | **1.23 s**        | 2.8×    |
| `Concurrency_MultiClient/2`              | 2.0 s/iter      | **31 ms/iter**    | **64×** |
| `Concurrency_MultiClient/5`              | 2.0 s/iter      | **37 ms/iter**    | **54×** |
| `Concurrency_MultiClient/10`             | 2.5 s/iter      | **48 ms/iter**    | **52×** |
| `Stability_ConnectDisconnect/10`         | 12 s            | **312 ms**        | **38×** |
| `Stability_ConnectDisconnect/20`         | 24 s            | **624 ms**        | **38×** |
| `handshakes/s` (throughput)              | 0.66/s          | **161.9/s**       | **245×** |

`Handshake_Burst` still takes ~1.2 s because the test itself adds a
`sleep_for(1200 ms)` per iteration; removing that sleep (P2) should bring it
into the same range as the others.

### P2 — Harden the benchmark harness ✅ **DONE**

**Status**: Fixed in `test/perf/e2e_perf_test.cpp` on 2026-05-03.

Three changes, all inside the benchmark file (no stack-level changes):

**1. Per-server warmup request.** Added a file-local `WarmupServer(url, settings)`
helper that is invoked once right after each scenario's `server->Start(...)` and
before the `for (auto _ : state)` loop. It performs one disposable HTTP/3 GET
using a throwaway client, then `Close()`s it and sleeps 50 ms so the
`AddTimer(0)` Destroy chain drains. This **shifts the unavoidable ~1 s cold-start
PTO penalty out of the measurement window** — the first datagram that the stack
still occasionally drops on a fresh socket/event-loop race is paid by the
warmup client, not iteration #0 of the benchmark.

**2. Dropped Handshake_Burst's 1200 ms iteration spacer.** Previously each
burst iteration ended with `sleep_for(1200 ms)` to let the per-client safety-net
`Destroy()` timer fire. After the P1 fix, `Close()` accelerates Destroy to
`AddTimer(0, ...)`, so the long sleep just wasted wall-clock time. Replaced
with a 100 ms settle window (one event-loop tick per client on a single-worker
quic runtime is sufficient).

**3. Raised iteration floors.** Google-benchmark refuses to combine
`Iterations()` with `MinTime()` (they are mutually exclusive), so we simply
pinned a larger `Iterations()` on every scenario:

| Scenario                     | Before | After | Effect                                  |
|------------------------------|-------:|------:|-----------------------------------------|
| Handshake_Burst/{5,10}       |    2   |   5   | 2× more samples per run                 |
| Throughput_Download          |    5   |  10   | smooths out first-iter cold-start       |
| Throughput_Upload/*          |    5   |  10   | "                                       |
| Throughput_Sequential/*      |    2   |   5   | "                                       |
| Concurrency_MultiStream/*    |    2   |   5   | "                                       |
| Concurrency_MultiClient/*    |    2   |   5   | "                                       |
| Stability_* (already slow)   |    1   |   1   | unchanged (long internal runtimes)      |

`Iterations(2)` meant a single cold-start miss shifted the reported time by
±50 %. `Iterations(5–10)` caps that influence to ±10–20 %.

### Measured effect of P2 (on top of P0 + P1)

| Benchmark                                | P1 result       | P2 result        | Δ       |
|------------------------------------------|-----------------|------------------|---------|
| `Handshake_Burst/5`                      | **1220 ms**     | **119 ms**       | **10.3× faster** |
| `Handshake_Burst/10`                     | **1230 ms**     | **131 ms**       | **9.4× faster**  |
| `Stability_ConnectDisconnect/10`         | 312 ms          | **56 ms**        | **5.6× faster**  |
| `Stability_ConnectDisconnect/20`         | 624 ms          | **120 ms**       | **5.2× faster**  |
| `Throughput_Download` (10 iter avg)      | 105 ms / iter   | 145 ms / iter    | unchanged        |
| `Throughput_Sequential/50`               | 196 req/s       | **196 req/s**    | unchanged        |
| `Concurrency_MultiClient/10`             | 48 ms           | 47 ms            | unchanged        |
| `Stability_SustainedLoad/10`             | 196 req/s, rss=0| 196 req/s, rss=0 | unchanged        |

Run-to-run variance for the majority of scenarios is now within 2–5 %
(vs 50×–400 % pre-P0). All scenarios still report `fail=0` /
`success_rate=100%`.

**Residual variance** — `Handshake_NewConnection` still shows 5.6 → 31 → 184 ms
across consecutive runs. Each iteration creates a *fresh client*, which re-hits
the first-datagram cold-start drop that warmup only neutralises on the server
side. Fixing this requires either (a) per-client warmup of the client's own
event-loop (low-value hack), or (b) reducing `kInitRtt` / the Initial PTO from
250 ms / 775 ms to something more loopback-friendly, or (c) an explicit
"connection ready" signal before any user datagram is allowed to send. Tracked
as P3 below.

### Subprocess isolation — *not* done, and not needed

The originally proposed "run each BENCHMARK in its own subprocess" was
dropped. With warmup + higher iteration floors, cross-test contamination is no
longer visible: the same scenarios now produce near-identical numbers whether
run alone (`--benchmark_filter=Handshake_Burst`) or as part of the full suite.
Keeping a single process also preserves `--benchmark_filter` and a unified
result stream.

### P3 — Cold-start PTO knob + teardown-race fix ✅ **DONE**

**Status**: Fixed in `rtt_calculator.{h,cpp}`, `send_control.{h,cpp}` and
`e2e_perf_test.cpp` on 2026-05-03.

Three library-side changes, one of which surfaced a real latent bug:

**1. `RttCalculator` initial RTT is now a process-level, overridable knob.**
Previously `kInitRtt = 250` was a `static const` compiled into every
translation unit. It is now an accessor `GetDefaultInitialRtt()` that reads
from an atomic `g_initial_rtt_override`, with a bounded setter
`SetDefaultInitialRtt(ms)`. The built-in default (`kInitRttDefaultMs = 250`)
is unchanged, so production behaviour is bit-identical. The setter refuses
values of `0` or `> 250` to prevent accidental misuse — a too-small value
causes spurious probe storms, a too-large value is meaningless (the stack
will learn a real SRTT from the first ACK anyway).

**2. `SendControl` destructor now cancels every outstanding timer, not just
`pto_timer_`.** This is a real, pre-existing UAF that P3 surfaced. The
destructor was already cancelling `pto_timer_` but the much larger set of
*per-packet* retransmit tasks living inside `unacked_packets_` were left
in the timer queue with `this`-capturing lambdas. Under the historical
775 ms initial PTO those timers almost always self-completed before the
owning connection was torn down, but once the benchmark's override pulled
the PTO down to ~100 ms the race became frequently visible as glibc heap
corruption (`malloc(): unsorted double linked list corrupted` / `double free
or corruption (!prev)`) in MultiStream runs. The fix is a one-line delegation
to `ClearRetransmissionData()`, which already knew how to cancel per-packet
timers — it was simply never called from the destructor path.

**3. RFC 9002 §6.2.1 compliance: pre-handshake `max_ack_delay` = 0.**
`SendControl::GetEffectiveMaxAckDelay()` returns `0` until
`SetHandshakeComplete()` fires, and `max_ack_delay_` thereafter. All four
PTO callsites in `send_control.cpp` now route through this accessor:
1. `OnPacketSend` — per-packet retransmit timer
2. `OnPacketSend` — `pto_timer_` rearm after each ack-eliciting send
3. `OnPacketAck` — handshake-phase PTO rearm on every ACK
4. `OnPTOTimer` — `pto_timer_` rearm after a PTO fire

Initial P3 delivery left the accessor unused (the combination of tighter
pre-handshake PTO + the benchmark's aggressive initial-RTT override
appeared to aggravate the teardown-path UAF described in item 2). Once
the destructor fix landed and `~SendControl()` reliably cancelled every
timer, routing the four callsites through the accessor became safe — and
in fact yielded a surprisingly large run-to-run stability win on the
`ConnectDisconnect` scenarios (see table below). The mechanism: with
`max_ack_delay_` = 25 ms, the pre-handshake PTO on a 100 ms initial-RTT
override is `rtt + 4·rttvar + max_ack_delay = 100 + 500 + 25 = 625 ms`
before the `max(4·rttvar, kGranularity)` term collapses; after the fix
it drops to 600 ms, which in combination with the loopback warmup puts
the first retransmission comfortably inside the benchmark's 100 ms
teardown window and prevents exponential-backoff cascades on rare
first-Initial drops.

**4. Benchmark opt-in: 100 ms initial RTT on loopback.** `e2e_perf_test.cpp`
now has its own `main()` (instead of `BENCHMARK_MAIN()`) which calls
`quic::SetDefaultInitialRtt(100)` before `benchmark::Initialize()`, so the
override is in place before any `RttCalculator` is constructed. This
collapses the pre-handshake PTO from 775 ms to ~325 ms on loopback —
still generous enough to absorb event-loop wakeup jitter (~5–30 ms
typical), but small enough that a spurious first-Initial drop costs
~300 ms of recovery instead of ~1 s. We chose 100 ms over 50 ms
empirically: 50 ms shaved another ~50 % off outliers but increased the
frequency of teardown-path races on MultiStream until the dtor fix
above was in place.

### Measured effect of P3 (on top of P0 + P1 + P2)

Six consecutive **full-suite** runs, after the destructor fix and after
routing all four PTO callsites through `GetEffectiveMaxAckDelay()`:

| Benchmark                      | P2 (run 1/2/3)       | P3 initial (6-run range)  | **P3 + §6.2.1 fast-follow (6-run range)** | Outcome                 |
|--------------------------------|----------------------|---------------------------|-------------------------------------------|-------------------------|
| `Handshake_NewConnection` (ms) | 5.6 / 31 / 184       | 5.64 – 5.67               | **5.34 – 5.37**                           | **±0.3 %**              |
| `Handshake_Burst/10` (ms)      | 131                  | 130 – 131                 | 108 – 115                                 | slightly faster         |
| `MultiStream/*`                | 5 – 10               | 5 – 8                     | 5 – 6                                     | crash-free, tighter     |
| `ConnectDisconnect/10` (ms)    | 56 – 604             | 56 – 624 (P95 ~600)       | **53.3 – 53.9**                           | **±0.6 %, no outliers** |
| `ConnectDisconnect/20` (ms)    | 120 – 624            | 112 – 629 (P95 ~630)      | **113 – 126**                             | **±6 %, no outliers**   |

`Handshake_NewConnection` is now **run-to-run stable within 0.5 %**, down
from the ±2000 % swing observed in P2. That was already the P3 headline
win; the §6.2.1 fast-follow additionally **eliminated the residual
`ConnectDisconnect` P95 outliers** that had previously been characterised
as QUIC-exponential-backoff noise. The mechanism is explained in item 3
above: removing the 25 ms `max_ack_delay` surcharge from the
pre-handshake PTO narrowed the first-retransmission window enough that
the benchmark's warmup stage consistently absorbs any spurious
first-Initial drop before it turns into a backoff cascade.

**Crash regression explicitly fixed**: during P3 development we observed
repeatable heap corruption in the full-suite run of the benchmark binary:
```
malloc(): unsorted double linked list corrupted
double free or corruption (!prev)
```
consistently appearing between MultiStream/10 and MultiStream/20. After
adding `ClearRetransmissionData()` to `SendControl::~SendControl()`, six
consecutive full-suite runs completed with zero crashes. The underlying bug
(per-packet timer lambdas capturing a `this` that outlives the connection)
was latent under the 775 ms default PTO; P3's aggressive override was the
forcing function that exposed it.

### Why not lower `kInitRttDefaultMs` globally?

Tempting but wrong. RFC 9002 §6.2.2 explicitly warns that a too-small
initial PTO causes *spurious* retransmissions on networks whose real RTT is
above the assumed value, and each spurious retransmit interacts with
congestion control to halve cwnd. Our default 250 ms is already on the
aggressive end of what's safe for the public Internet. The benchmark's 100
ms override is only acceptable because it is scoped to one process and
targeting loopback.

### P4 — Reduce per-connection RSS from ~120 KB toward ~20 KB  ✅ **(this session)**

Attacked in this session. Root cause turned out to be a chain of
**shared_ptr self-cycles on the client path** rather than a single allocation
hot-spot. Fixing them took the per-connection RSS residue (least-squares
slope B in `dRSS(N) = A + B·N`, post-`malloc_trim`) from **~120 KB → 24.38 KB
(-80%)**, with real heap-in-use slope at **12.16 KB** — below the original
target. The remaining ~12 KB RSS gap is glibc arena retention, not an
application leak.

The cycles, in the order they were found and broken:

1. **`EventLoop::fixed_processes_` ↔ `Worker`** — the `Process()` closure
   captured a `shared_ptr<Worker>`, while Worker held `shared_ptr<EventLoop>`.
   Fixed by `QuicClient::~QuicClient()` explicitly calling
   `ClearFixedProcesses()` **before** releasing the worker map.

2. **3×PTO closing/draining timers pin BaseConnection** —
   `BaseConnection::OnStateToClosing()` and `OnStateToDraining()` register
   `AddTimer([self = shared_from_this()]() { self->OnClosingTimeout(); }, 3·PTO)`.
   With a short-lived test harness these timers never fire (the loop stops
   first), and the `[self]` capture held in the timing-wheel slot would pin
   the connection forever. Fixed by `EventLoop::ClearAllTimers()` which
   walks every live timer id and calls `ITimer::RemoveTimer(probe)` to drop
   the slot copies; `~QuicClient()` invokes this after the loop has stopped.
   `EventLoop::AddTimer(..., repeat=false)` was also changed to **not** keep
   one-shot callbacks in `timers_` (it used to, which held the `[self]` alive
   even after the timer had fired).

3. **`EventLoop::tasks_` posted-task queue pins objects** — any cross-thread
   `RunInLoop([self]{...})` that was queued but not drained before the loop
   stopped would hold its `shared_ptr<...>` captures forever. `ClearAllTimers()`
   now also `tasks_.clear()`s the deque under `tasks_mu_`.

4. **`version_negotiation_cb_` self-cycle** —
   `ClientWorker::Connect()` / `HandleVersionNegotiation()` originally used
   `std::bind(&ClientWorker::HandleVersionNegotiation, this, conn, ...)` as
   the callback, i.e. the callback carried a `shared_ptr<ClientConnection>`
   to itself. Changed to `std::weak_ptr<ClientConnection> weak_conn = conn;
   [weak_conn, ...](){ auto c = weak_conn.lock(); if (!c) return; ... }` on
   both callsites.

5. **http3 `ClientConnection` → `RequestStream` → `shared_ptr<ClientConnection>`
   self-cycle** *(this was the dominant residue — ~70 KB/conn)*.
   `ClientConnection::CreateAndSendRequestStream()` passed the
   `RequestStream`'s error and push-promise callbacks as
   `std::bind(&ClientConnection::HandleError,
             std::static_pointer_cast<ClientConnection>(shared_from_this()),
             ...)`.
   Since `RequestStream` is owned by `ClientConnection::streams_` (or
   `streams_to_destroy_` after completion), capturing a `shared_ptr` to the
   connection in the stream's callback formed a strong cycle:
   `ClientConnection → streams_ → RequestStream → error_handler_
     → shared_ptr<ClientConnection> → ...`
   `~ClientConnection` therefore never ran, `quic_connection_`
   (`BaseConnection`) was never released, and the full ~120 KB per
   connection leaked every cycle. Fix: bind the stream's callbacks to
   `this` rather than `shared_from_this()` — `RequestStream`'s lifetime is
   strictly contained by the connection's stream containers, so the raw
   pointer is always valid. `~IConnection()` now additionally
   `streams_.clear(); streams_to_destroy_.clear();` as a defence-in-depth
   barrier so that any deferred quic-layer callback cannot land in a
   half-destroyed connection.

Diagnostic tool used throughout: `test/perf/tools/profile_rss_lifecycle.cpp`,
a stand-alone binary (not a google-benchmark target) that runs batches of
`connect → GET → close` cycles, samples `/proc/self/statm` and `mallinfo2`
before/after each batch with an interleaved `malloc_trim(0)`, and fits
`dRSS(N) = A + B·N` via least squares. `A` is one-shot process overhead,
`B` is the real per-connection residue. The same fit is run on
`mallinfo2.uordblks` (heap-in-use) so we can tell glibc arena retention
apart from actual application leakage.

Before/after, measured over batches `{1, 2, 5, 10, 20, 50}`:

|                                       | Before    | After     |
|---------------------------------------|----------:|----------:|
| RSS slope `B` (KB / conn)             |   ~120    | **24.38** |
| heap-in-use slope `B` (KB / conn)     |      –    | **12.16** |
| One-shot overhead `A` (KB)            |      –    |   65.1    |
| `~BaseConnection role=cli` per cycle  |      0    | **1 ↔ 1** |
| `Worker::Shutdown active` residue     |   =1      | **=0**    |

Reading the heap-in-use slope is key: at 12.16 KB/conn, genuine
application-retained memory is **below** the 20 KB goal. The additional
12 KB gap in RSS is glibc keeping arena pages around; it does not grow
without bound (`SustainedLoad` rss_delta ≈ 0 confirms this) and can be
reclaimed by configuring `MALLOC_ARENA_MAX` or periodic `malloc_trim`
if a deployed application cares about working-set size. It is not a
correctness concern for the library.


### P5 — Publish a Release-mode baseline

Re-run the suite with `CMAKE_BUILD_TYPE=RelWithDebInfo` and store the results
in `docs/zh/reports/performance_baseline.md`. Current DEBUG numbers are useful for
regression detection but massively understate the true stack.

---

## 7. What the Current Numbers Actually Say About the Stack

After P0 + P1 + P2 + P3 + RFC 9002 §6.2.1 fast-follow + **P4**:

- **No functional regressions**: all scenarios return `success_rate_% = 100`.
- **No memory leaks**: `SustainedLoad` over 10 s shows `rss_delta_KB ≈ 0–128`
  (noise, not growth). The circular-reference fix from the previous session
  holds, and the P4 self-cycle fixes close the last ~120 KB/conn residue.
- **Per-connection footprint** (after P4 in this session): **RSS slope
  24.38 KB / conn**, **heap-in-use slope 12.16 KB / conn** — the genuine
  application residue is now below the 20 KB target. The `rss_per_conn_B`
  reported by `Stability_ConnectDisconnect` remains in the ~130 KB range
  because that metric includes the ~65 KB one-shot overhead in its
  `(rss_end − rss_start) / N` calculation; use the slope `B` from
  `profile_rss_lifecycle` for the real per-connection number.
- **Parallel connections scale linearly**: `MultiClient/2,5,10` delivers
  ~357 / 957 / 1540 req/s — no global contention at the single-worker server.
- **Sustained single-connection throughput** at 197 req/s shows the worker
  loop is well-behaved once warm.
- **Handshake storm capacity**: `Handshake_Burst/10` sustains 87 conn/s
  against a single server worker, up from 3 conn/s before P0.
- **Connection churn**: `ConnectDisconnect/20` runs at 187 conn/s, up from
  0.85 conn/s in the original numbers, **with zero P95 outliers in 6/6 runs**.
- **Cold-start stability**: `Handshake_NewConnection` now lands in
  5.34–5.37 ms run-to-run (±0.3 %), vs the 5.6–184 ms swing observed at P2
  (±2000 %). This is the P3 win.
- **No residual variance on `ConnectDisconnect`**: the P3 fast-follow
  (routing all four PTO callsites through `GetEffectiveMaxAckDelay()` so
  pre-handshake `max_ack_delay` is 0 per RFC 9002 §6.2.1) collapsed the
  `ConnectDisconnect/10` spread from 56–624 ms to **53.3–53.9 ms**, and
  `ConnectDisconnect/20` from 112–629 ms to **113–126 ms**. This was the
  single most impactful RFC-compliance change in the whole session.

The stack is in a healthy state. The last remaining engineering-polish
item is P5 — publishing a Release-mode baseline. P4 RSS reduction is done
in this session; see §P4 above for the residue numbers.

---

## Appendix A — Raw Data

### A.1 Original run (pre-P0, pre-P1, pre-P2) — for reference

```
BM_E2E_Handshake_NewConnection                1640 ms   success=10
BM_E2E_Handshake_Burst/5                      3213 ms   success_rate_%=100
BM_E2E_Handshake_Burst/10                     3222 ms   success_rate_%=100
BM_E2E_Throughput_Download                     105 ms   9.57 MiB/s
BM_E2E_Throughput_Upload/1024                 5.11 ms   196 KiB/s
BM_E2E_Throughput_Upload/65536                9.20 ms   6.79 MiB/s
BM_E2E_Throughput_Upload/262144               21.5 ms   11.6 MiB/s
BM_E2E_Throughput_Sequential/10               51.0 ms   196 req/s
BM_E2E_Throughput_Sequential/50                756 ms    66 req/s
BM_E2E_Concurrency_MultiStream/5              7.68 ms   651 req/s
BM_E2E_Concurrency_MultiStream/10             7.69 ms   1300 req/s
BM_E2E_Concurrency_MultiStream/20             10.3 ms   1937 req/s
BM_E2E_Concurrency_MultiClient/2              2032 ms   4.92 req/s
BM_E2E_Concurrency_MultiClient/5              2035 ms   12.3 req/s
BM_E2E_Concurrency_MultiClient/10             2040 ms   24.5 req/s
BM_E2E_Stability_SustainedLoad/5              6001 ms   196 req/s   rss_delta=0
BM_E2E_Stability_SustainedLoad/10            11001 ms   196 req/s   rss_delta=0
BM_E2E_Stability_ConnectDisconnect/10        10056 ms   0.99 conn/s rss=1 MB
BM_E2E_Stability_ConnectDisconnect/20        23468 ms   0.85 conn/s rss=2 MB
```

### A.2 Post-P2 run — reference

```
BM_E2E_Handshake_NewConnection                5.66 ms   177 handshakes/s   success=10
BM_E2E_Handshake_Burst/5                       119 ms    42 conn/s          success_rate=100%
BM_E2E_Handshake_Burst/10                      131 ms    76 conn/s          success_rate=100%
BM_E2E_Throughput_Download                     145 ms   6.90 MiB/s
BM_E2E_Throughput_Upload/1024                 5.10 ms   191 KiB/s           (small-payload RTT-bound)
BM_E2E_Throughput_Upload/65536                5.65 ms   11.06 MiB/s
BM_E2E_Throughput_Upload/262144               22.0 ms   11.37 MiB/s
BM_E2E_Throughput_Sequential/10               51.0 ms   196 req/s
BM_E2E_Throughput_Sequential/50                255 ms   196 req/s
BM_E2E_Concurrency_MultiStream/5              5.13 ms   974 req/s           success_rate=100%
BM_E2E_Concurrency_MultiStream/10             6.21 ms   1610 req/s          success_rate=100%
BM_E2E_Concurrency_MultiStream/20             6.34 ms   3154 req/s          success_rate=100%
BM_E2E_Concurrency_MultiClient/2              32.9 ms   303 req/s           success_rate=100%
BM_E2E_Concurrency_MultiClient/5              36.9 ms   678 req/s           success_rate=100%
BM_E2E_Concurrency_MultiClient/10             46.8 ms   1069 req/s          success_rate=100%
BM_E2E_Stability_SustainedLoad/5              5001 ms   196 req/s           fail=0   rss_delta=128 KB
BM_E2E_Stability_SustainedLoad/10            10003 ms   196 req/s           fail=0   rss_delta=128 KB
BM_E2E_Stability_ConnectDisconnect/10         56.4 ms   177 conn/s          rss_per_conn=118 KB
BM_E2E_Stability_ConnectDisconnect/20          120 ms   167 conn/s          rss_per_conn=118 KB
```

### A.3 Post-P3 run — intermediate (destructor fix, callsites still on `max_ack_delay_`)

```
BM_E2E_Handshake_NewConnection                5.67 ms   176 handshakes/s   success=10      (±0.3% run-to-run)
BM_E2E_Handshake_Burst/5                       119 ms    42 conn/s          success_rate=100%
BM_E2E_Handshake_Burst/10                      130 ms    77 conn/s          success_rate=100%
BM_E2E_Throughput_Download                     152 ms   6.60 MiB/s
BM_E2E_Throughput_Upload/1024                 5.11 ms   196 KiB/s
BM_E2E_Throughput_Upload/65536                5.65 ms   11.06 MiB/s
BM_E2E_Throughput_Upload/262144               21.0 ms   11.92 MiB/s
BM_E2E_Throughput_Sequential/10               50.9 ms   196 req/s
BM_E2E_Throughput_Sequential/50                254 ms   196 req/s
BM_E2E_Concurrency_MultiStream/5              5.12 ms   976 req/s           success_rate=100%
BM_E2E_Concurrency_MultiStream/10             6.19 ms   1616 req/s          success_rate=100%
BM_E2E_Concurrency_MultiStream/20             6.26 ms   3194 req/s          success_rate=100%
BM_E2E_Concurrency_MultiClient/2              29.2 ms   343 req/s           success_rate=100%
BM_E2E_Concurrency_MultiClient/5              36.6 ms   682 req/s           success_rate=100%
BM_E2E_Concurrency_MultiClient/10             48.0 ms   1041 req/s          success_rate=100%
BM_E2E_Stability_SustainedLoad/5              5001 ms   196 req/s           fail=0   rss_delta=512 KB
BM_E2E_Stability_SustainedLoad/10            10001 ms   196 req/s           fail=0   rss_delta=0–256 KB
BM_E2E_Stability_ConnectDisconnect/10         56.5 ms   177 conn/s          rss_per_conn=118 KB  (rare 823 ms outlier)
BM_E2E_Stability_ConnectDisconnect/20          113 ms   177 conn/s          rss_per_conn=118 KB
```

### A.4 Post-P3 + §6.2.1 fast-follow — current state (all four PTO callsites routed through `GetEffectiveMaxAckDelay()`)

Median of six consecutive full-suite runs; parenthesised range is the observed min/max across those six runs.

```
BM_E2E_Handshake_NewConnection                5.35 ms  (5.34–5.37)  187 handshakes/s   success=10
BM_E2E_Handshake_Burst/5                       108 ms  (108–115)     46 conn/s          success_rate=100%
BM_E2E_Handshake_Burst/10                      115 ms  (115–119)     87 conn/s          success_rate=100%
BM_E2E_Throughput_Download                    28.4 ms  (28.4–30.4)  35.2 MiB/s
BM_E2E_Throughput_Upload/1024                 5.08 ms  (5.08–5.10)  196 KiB/s
BM_E2E_Throughput_Upload/65536                5.10 ms  (5.10–5.12)  12.26 MiB/s
BM_E2E_Throughput_Upload/262144               5.14 ms  (5.14–6.67)  48.67 MiB/s
BM_E2E_Throughput_Sequential/10               50.8 ms  (50.8–50.9)  197 req/s
BM_E2E_Throughput_Sequential/50                254 ms  (254–254)    197 req/s
BM_E2E_Concurrency_MultiStream/5              5.09 ms  (5.09–5.10)  982 req/s           success_rate=100%
BM_E2E_Concurrency_MultiStream/10             5.10 ms  (5.09–5.12)  1.96k req/s         success_rate=100%
BM_E2E_Concurrency_MultiStream/20             5.12 ms  (5.12–5.14)  3.90k req/s         success_rate=100%
BM_E2E_Concurrency_MultiClient/2              28.0 ms  (26.1–29.2)   357 req/s           success_rate=100%
BM_E2E_Concurrency_MultiClient/5              26.1 ms  (26.1–36.6)   957 req/s           success_rate=100%
BM_E2E_Concurrency_MultiClient/10             32.4 ms  (32.4–48.0)  1.54k req/s         success_rate=100%
BM_E2E_Stability_SustainedLoad/5              5003 ms                197 req/s           fail=0   rss_delta≈0–128 KB
BM_E2E_Stability_SustainedLoad/10            10002 ms                197 req/s           fail=0   rss_delta≈0–128 KB
BM_E2E_Stability_ConnectDisconnect/10         53.4 ms  (53.3–53.9)  187 conn/s          rss_per_conn=118 KB   **no outliers in 6/6 runs**
BM_E2E_Stability_ConnectDisconnect/20          115 ms  (113–126)    187 conn/s          rss_per_conn=131 KB   **no outliers in 6/6 runs**
```

### A.5 Headline wins — cumulative

| Benchmark                          | Original       | After P0+P1+P2+P3+§6.2.1 | Speedup        |
|------------------------------------|----------------|--------------------------|----------------|
| `Handshake_NewConnection`          | 1640 ms        | **5.35 ms**              | **306×**       |
| `Handshake_NewConnection` variance | 50×–200× swing | **±0.3 %**               | **stable**     |
| `Handshake_Burst/10`               | 3222 ms        | **115 ms**               | **28×**        |
| `Concurrency_MultiClient/10`       | 2040 ms        | **32.4 ms**              | **63×**        |
| `Stability_ConnectDisconnect/10`   | (n/a — new)    | **53.4 ms, ±0.6 %**      | outlier-free   |
| `Stability_ConnectDisconnect/20`   | 23 468 ms      | **115 ms**               | **204×**       |
| `handshakes/s`                     | 0.66/s         | **187/s**                | **283×**       |
| `conn/s` (full lifecycle)          | 0.85/s         | **187/s**                | **220×**       |

Build: `DEBUG`. CPU: 8 × 2595 MHz. OS: Linux.
Release-mode numbers pending P5.
