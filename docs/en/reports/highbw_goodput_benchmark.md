# Cross-Implementation High-Bandwidth Goodput Benchmark (Self-Pair)

This document records a self-hosted throughput comparison built on the official `quic-interop-runner` framework, with the bandwidth tier raised well above the official 10Mbps standard.

The goal is to compare quicX against 9 mainstream QUIC implementations under conditions closer to real high-speed networks, and to verify whether quicX's own congestion-control / flow-control configuration is already tuned optimally.

This does not conflict with the official `goodput` (10Mbps) results in [`reports/interop_status.md`](interop_status.md) — it is a supplementary experiment at higher bandwidth tiers. **Not part of the official matrix, non-contractual.**

---

## 1. Background & Motivation

The official `quic-interop-runner` `goodput` measurement is fixed at `simple-p2p --delay=15ms --bandwidth=10Mbps --queue=25`. This bandwidth tier is too low to reflect quicX's real throughput in file-transfer / streaming scenarios. A self-test script `run_highbw.py` (in the `quic-interop-runner/` repo) reuses the official framework's capabilities with only two changes:

1. Raise `MeasurementGoodput`'s ns3 link bandwidth parameter from 10Mbps to an arbitrary value;
2. Replace the default result-verification path (`pyshark`, which builds Python objects packet-by-packet and takes minutes for large files) with native `tshark -T fields` single-pass field extraction (seconds) — the timing method itself matches the official one: the timestamp delta between the first and last 1-RTT data packet captured at the sim node.

Everything else — container orchestration, ns3 topology, `docker-compose.yml`, certificate generation — reuses the official `quic-interop-runner` logic as-is. **This is not a reimplementation.**

---

## 2. Key Finding: The Simulator Itself Has a Forwarding Ceiling

Before pushing bandwidth higher, a probing round surfaced something more important than "which implementation is faster."

### 2.1 Symptom: Efficiency Below 30% at 1Gbps

| Bandwidth tier | quicx goodput | Theoretical utilization |
|---|---|---|
| 100Mbps | 92.9 Mbps | 92.9% |
| 1Gbps | 259.5 Mbps | 25.9% |

All 10 implementations converge into the 180–260Mbps band at the 1Gbps tier — efficiency collapses as bandwidth rises, rather than staying at a fixed ratio.

### 2.2 Root-Cause Investigation

- **No packet loss / retransmission**: transferring the same 100MB file at 1Gbps vs. 100Mbps produces nearly identical total QUIC packet counts (80059 vs. 82757) — the higher tier does not trigger extra retransmission, so congestion control backing off is not the cause.
- **Real RTT far exceeds the configured value**: `--delay 1ms` is only the per-segment one-way link delay. The pcap-measured full round trip (Initial → Initial+ACK) is about **8.3ms** — over 8× the configured value — because ns3's `RealtimeSimulatorImpl` + `EmuFdNetDevice` scheduling/queuing overhead stacks on top of the link delay.
- **Back-calculated effective windows cluster tightly**: `window = throughput × RTT / 8` gives quicx ≈263KB, msquic ≈265KB, quic-go ≈244KB, lsquic ≈186KB — matching the order of magnitude of each implementation's default (untuned) flow-control window, and 263KB ÷ 8.3ms ≈ 253Mbps lines up almost exactly with the observed 1Gbps-tier ceiling.
- **Simulator forwarding-capacity probe**: the 10Gbps tier returns `unsupported` outright (exits within 3.6s); the 300Mbps and 500Mbps tiers produce nearly identical measured throughput (257 vs. 262Mbps) — indicating the real bottleneck is the **single-process ns3 real-time forwarding pipeline** (which must feed every real packet on both veth NICs into the discrete-event simulator in real time, while `dumpcap` is also capturing full traffic on both sides), not the protocol stack of any single QUIC implementation.
- **Cross-validation**: 10 implementations in entirely different languages/architectures (Go/Rust/C/C++) all converge into the same narrow 180–260Mbps band at 1Gbps — if the bottleneck were each implementation's own flow-control window, their independent defaults could not plausibly all land in the same band by coincidence; but if the bottleneck is a shared simulator forwarding capacity, all implementations hitting the same ceiling is exactly what's expected.

**Conclusion**: the ns3 simulated link's real forwarding ceiling sits at **~260Mbps**, well below the configured link-bandwidth cap. Tuning any implementation's flow-control/congestion parameters above this ceiling produces no observable improvement — the throttle simply isn't in the protocol stack.

### 2.3 Choosing a Safe Zone

| Bandwidth tier | quicx goodput | Efficiency |
|---|---|---|
| 100Mbps | 92.9 Mbps | 92.9% |
| 150Mbps | — | 90%+ (probe passed) |
| 200Mbps | 182.3 Mbps | 91.2% |
| 300Mbps | ~257 Mbps | 85.7% (already at ceiling) |
| 500Mbps | ~262 Mbps | 52.4% (already at ceiling) |

**200Mbps** was chosen as the safe zone for the comparison and tuning that follows — 25%+ headroom below the ceiling, where differences in each implementation's flow-control/congestion algorithm show up genuinely in the numbers rather than being masked by the simulator's own capacity limit.

---

## 3. 200Mbps Safe-Zone Baseline: 10-Implementation Self-Pair

- Link: `200Mbps`, delay `1ms` (measured RTT ≈8ms), queue `1000`
- Transfer: `100MB`, 3 repetitions per implementation
- Pairing: self-pair (same implementation on both client and server), to avoid mixing in "who's more compatible with whom" as a confounding variable
- Timing: sim-side capture, timestamp delta between the first and last 1-RTT data packet (matches the official `MeasurementGoodput` method)

| Implementation | Goodput (Mbps) | stdev | Efficiency (/200Mbps) |
|---|---|---|---|
| picoquic | 188.4 | 0.7 | 94.2% |
| lsquic | 182.6 | 4.7 | 91.3% |
| **quicx** | **182.3** | **0.4** | **91.2%** |
| msquic | 181.4 | 6.9 | 90.7% |
| quic-go | 179.3 | 13.1 | 89.7% |
| quinn | 173.6 | 6.6 | 86.8% |
| ngtcp2 | 150.0 | 24.4 | 75.0% |
| quiche | 139.4 | 52.4 | 69.7% |
| xquic | 65.6 | 1.0 | 32.8% |
| aioquic | 50.5 | 0.8 | 25.3% |

**Observations**:

- quicx ranks third at 182.3Mbps, in the same tier as picoquic / lsquic, with a stdev of only 0.4 — the lowest of all 10 implementations, indicating quicx's throughput in this safe zone is **highly stable and reproducible**.
- quiche's stdev is as high as 52.4 (a clear outlier among its 3 repetitions), and ngtcp2 also shows relatively high variance (24.4) — possibly related to each implementation's default congestion-control convergence characteristics, or host-scheduling jitter during one repetition; not investigated further here (outside quicx's scope).
- xquic / aioquic show markedly lower efficiency (<35%), likely due to a comparatively small default flow-control window relative to this BDP; since both are black-box QNS images (see next section), no further tuning verification was attempted.

---

## 4. quicX Parameter Tuning: Congestion-Control Algorithm Sweep

### 4.1 Tunability Survey Across Implementations

Before "tune then re-test," the `run_endpoint.sh` of all 10 official QNS images was surveyed for exposed tuning hooks:

| Implementation | Tuning hook | Note |
|---|---|---|
| **quicx** | Source-level change + new `QUICX_CC` env var | Own project — hook added directly |
| picoquic / msquic / lsquic | `SERVER_PARAMS` / `CLIENT_PARAMS` env vars pass through arbitrary CLI args | Official image's built-in "backdoor," not exercised in this run |
| ngtcp2 | None needed | `run_endpoint.sh` already hardcodes `--cc bbr`, i.e. ships pre-tuned |
| quic-go / quiche / aioquic / xquic / quinn | None | `run_endpoint.sh` exposes no tuning switches at all — black-box images |

Conclusion: quicx is the only target that can be swept at low cost and with no risk. Making invasive changes to the other black-box images (forking and reverse-engineering their tuning knobs) is disproportionate in cost/risk and was not attempted this round.

### 4.2 quicX's Current Flow-Control Windows

The defaults in `QuicTransportParams` (`include/quicx/quic/type.h`) are already generous, purpose-built for high-throughput scenarios:

| Parameter | Default |
|---|---|
| `initial_max_data_` (connection-level) | 64 MB |
| `initial_max_stream_data_bidi_local_/_remote_` | 16 MB |
| `initial_max_stream_data_uni_` | 16 MB |

The BDP at 200Mbps × 8ms RTT is only ~200KB, far below the 16MB stream-level window — flow control **is not the bottleneck** in this scenario, so tuning this round focused on congestion-control algorithm selection instead.

### 4.3 Added `QUICX_CC` Environment Variable

To switch algorithms without rebuilding the image, the following was added to `test/interop/interop_server.cpp` and `interop_client.cpp`:

```cpp
// Congestion control algorithm override, for benchmarking without
// rebuilding: "reno" | "cubic" | "bbrv1" | "bbrv2" | "bbrv3".
const char* cc = std::getenv("QUICX_CC");
if (cc) {
    config.config_.congestion_control_ = cc;
}
```

`quic-interop-runner/docker-compose.yml`'s server/client environment blocks now pass through `QUICX_CC=$QUICX_CC`, and `run_highbw.py` gained a `--cc` CLI flag (injected via the `TestCase.additional_envs()` extension point, which is a no-op for other implementation images that don't recognize the variable).

### 4.4 Sweep Results

200Mbps safe zone, 5 algorithms, 3 repetitions each:

| Congestion-control algorithm | Goodput (Mbps) | stdev |
|---|---|---|
| reno | 183.2 | 0.8 |
| **cubic (current default)** | 182.1 | 0.8 |
| bbrv1 | 183.1 | 0.1 |
| bbrv2 | 182.2 | 1.1 |
| bbrv3 | **119.5** | 6.1 |

**Finding 1: reno / cubic / bbrv1 / bbrv2 are statistically equivalent** (182–183Mbps, differences within measurement error). This matches expectations — 200Mbps × 8ms RTT is a low-BDP scenario, and after slow start the link's 200Mbps cap is reached quickly, leaving little room for algorithms to differentiate on "how to approach the bandwidth ceiling." This also means quicX's current default configuration (cubic + a generous flow-control window) is **already optimal** for this scenario — neither switching the algorithm nor enlarging the window would produce a gain.

**Finding 2: bbrv3 regresses noticeably, at only 119.5Mbps (-35% vs. the other four algorithms)**. This is a genuine, worth-recording finding for follow-up — a plausible hypothesis is that BBRv3's ProbeRTT cycle in `cc_config.h` (triggered every 10s, holding a reduced window for 200ms) eats into a disproportionate share of this ~1-minute-long transfer, or that the pacing gain during Startup / bandwidth-probing phases is overly conservative for this particular BDP range. **This is a known but not-yet-fixed issue**, left for a future release to investigate (out of scope for this benchmark).

---

## 5. Bypassing the Simulator: Native Loopback Testing (No ns3, No Docker)

Section 2 already showed the ns3 simulated link itself has a ~260Mbps forwarding ceiling. To measure quicX's **real throughput ceiling, unconstrained by the simulator**, `quic-interop-runner`'s container orchestration needs to be bypassed entirely. There are two possible layers:

| Approach | Bypasses | Remaining overhead | Use case |
|---|---|---|---|
| `test/interop/docker-compose-direct.yml` (direct compose; removed along with the in-repo interop environment, recoverable from git history) | ns3 simulation and tc/netem shaping | docker bridge / veth forwarding overhead | Keeping container isolation, or comparing against other implementations still shipped as images |
| **Native host processes + loopback** (this section) | ns3 **and** docker networking | Only quicX's own protocol-stack processing cost | Measuring the absolute ceiling of "how fast the protocol stack itself can go" |

This section uses the latter: run `interop_server` / `interop_client` directly on the host, communicating over `127.0.0.1`, with no container or network simulation in between.

### 5.1 Method

1. Build the native binaries (no docker needed):
   ```bash
   cmake --build build --target interop_server interop_client -j$(nproc)
   ```
2. Use the new `test/interop/bench_loopback.sh` script to start the server, run the client N times, and aggregate mean/stdev:
   ```bash
   ./test/interop/bench_loopback.sh --size-mb 500 --reps 5 [--cc cubic]
   ```
3. **Timing method**: wall-clock time of the client process (start to exit) is used directly. On loopback, handshake RTT is under 1ms, negligible relative to a multi-second transfer, so there's no need for `tcpdump`/`tshark` packet-timestamp timing (which also often lacks `CAP_NET_RAW` permission in containerized CI environments anyway).

### 5.2 Results: quicX's Single-Connection Throughput Has Its Own Ceiling, Lower Than the ns3 Cap

| File size | Repetitions | Goodput (Mbps) | stdev |
|---|---|---|---|
| 100MB | 5 | 472.2 | 8.4 |
| 500MB | 5 | 633.9 | 18.2 |
| 1000MB | 3 | 638.7 | 11.6 |

- The 100MB figure is lower because fixed connection-setup/slow-start overhead still accounts for a non-trivial share of the total transfer time. The 500MB and 1000MB figures have already converged to the same steady value, **~635Mbps** — confirming this is the true steady-state ceiling, not still ramping up.
- **~635Mbps is markedly lower than what a loopback NIC can normally sustain (typically tens of Gbps)**, indicating the bottleneck is not the kernel network stack or docker, but quicX's own single-connection processing path (most likely per-packet encryption/decryption plus user/kernel-space copies and syscall overhead, processing a single connection on one thread). This connects directly to the GSO/sendmmsg/recvmmsg batched I/O path discussed in [`design/udp_io.md`](../design/udp_io.md), and gives a concrete, reproducible target for future performance work.

### 5.3 Congestion-Control Sweep: All 5 Algorithms Are Equivalent on Loopback, and the bbrv3 Anomaly Does Not Reproduce

500MB, 5 algorithms, 3 repetitions each:

| Algorithm | Goodput (Mbps) | stdev |
|---|---|---|
| reno | 625.7 | 5.3 |
| cubic | 624.1 | 18.4 |
| bbrv1 | 619.4 | 11.9 |
| bbrv2 | 631.1 | 15.8 |
| bbrv3 | 613.0 | 11.9 |

- All 5 algorithms land within each other's error margins — **consistent with, and a further confirmation of, §4.4**: quicX's throughput ceiling here is determined by single-connection processing cost, entirely independent of the congestion-control algorithm (on loopback RTT ≈0 and BDP is near-zero, so any algorithm fills the window almost instantly).
- **Key cross-validation**: the -35% bbrv3 anomaly seen in §4.4 at 200Mbps/8ms RTT **does not reproduce here** (RTT ≈0) — 613 vs. 619–631 for the other four, a difference within noise. This supports the earlier hypothesis: the bbrv3 regression is tied to **RTT / the ProbeRTT cycle**, not a general bug on the throughput path itself — narrowing the scope for future investigation.

---

## 6. Conclusions & Recommendations

1. **Beyond the official `goodput` (10Mbps) scenario, quicX achieves 91.2% throughput efficiency in the 200Mbps safe zone**, in the same tier as picoquic / lsquic / msquic, and with the lowest variance (stdev) of all implementations tested.
2. **The low efficiency observed at 1Gbps and above is caused by `quic-interop-runner`'s built-in ns3 simulator's forwarding-capacity ceiling (~260Mbps)**, not a deficiency in any QUIC implementation.
3. **After bypassing both ns3 and docker, quicX's native-loopback single-connection steady-state throughput ceiling is ~635Mbps** — higher than the simulator's ceiling, but far below what a loopback NIC can physically sustain. This points to a separate, environment-independent ceiling rooted in quicX's own protocol-stack processing cost — a concrete lead for future performance work (batched I/O, per-packet crypto overhead).
4. **quicX's current default configuration (cubic + 64MB/16MB flow-control windows) is already optimal for BDP-limited scenarios**; `QUICX_CC` and `bench_loopback.sh` are retained as permanent tuning/debugging tools for re-sweeping future scenarios.
5. **BBRv3 shows a clear throughput regression in scenarios with real RTT (especially involving the ProbeRTT cycle), but the regression does not reproduce on RTT≈0 loopback** — this narrows the investigation to RTT/ProbeRTT-related logic, left for a future fix.

---

## 7. Reproduction

```bash
cd quic-interop-runner

# 200Mbps safe-zone baseline, 10-implementation self-pair
python3 run_highbw.py \
  --impls quicx,quic-go,ngtcp2,quiche,msquic,lsquic,picoquic,xquic,quinn,aioquic \
  --bandwidth 200Mbps --delay 1ms --queue 1000 \
  --filesize-mb 100 --repetitions 3 \
  --out-dir highbw_results

# quicX congestion-control algorithm sweep (--cc only affects quicx; other
# implementations ignore the env var)
for cc in reno cubic bbrv1 bbrv2 bbrv3; do
  python3 run_highbw.py --impls quicx --cc $cc \
    --bandwidth 200Mbps --delay 1ms --queue 1000 \
    --filesize-mb 100 --repetitions 3 --out-dir /tmp/cc_sweep
  docker rm -f sim server client
done
```

> Note: fixed container names (`sim`/`server`/`client`) mean a leftover container from a previous run causes the next run to silently fail (empty result, no error). `run_highbw.py` now auto-cleans before `run_self_pair()`; when running multiple rounds manually, it's still recommended to run `docker rm -f sim server client` between rounds.

```bash
cd quicX

# build native binaries
cmake --build build --target interop_server interop_client -j$(nproc)

# native loopback baseline (no ns3, no docker)
./test/interop/bench_loopback.sh --size-mb 500 --reps 5

# native loopback congestion-control sweep
for cc in reno cubic bbrv1 bbrv2 bbrv3; do
  ./test/interop/bench_loopback.sh --size-mb 500 --reps 3 --cc $cc
done
```
