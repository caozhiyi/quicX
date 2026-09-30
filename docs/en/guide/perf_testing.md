# quicX Performance Testing Guide

This document covers all performance benchmarks and profiling tools under **`test/perf/`** in the quicX repo:

1. Test-suite overview and case inventory
2. Building and running
3. Common command templates (single case, JSON output, filtering, comparison)
4. Reference results and how to interpret them

For the **performance baseline** (the big numbers table) and the historical conclusions of the E2E root-cause analysis, continue with:

- [`../reports/performance_baseline.md`](../reports/performance_baseline.md) — numeric snapshots based on macOS ARM64 / Release

---

## 1. Test Organization and Case Inventory

All performance tests are based on [Google Benchmark v1.8.3](https://github.com/google/benchmark), registered by `test/perf/CMakeLists.txt` as standalone executables output to `build/bin/perf/`.

### 1.1 The Test Matrix

| Executable | Positioning | Main Coverage |
|---|---|---|
| `cpu_hotspot_test` | component-level CPU hotspots | TLS / Buffer / Frame / QPACK / Pool / packet-processing simulation |
| `memory_baseline_test` | memory baseline | Buffer footprint, Block Pool efficiency, long-run stability, Chain growth |
| `memory_pool_efficiency_test` | memory-pool comparison | `Poolallocator` / `BlockMemoryPool` / `std::malloc` under various loads |
| `crypto_perf_test` | protocol hot path P0 | AEAD (AES-128/256-GCM, ChaCha20-Poly1305), HKDF |
| `packet_perf_test` | protocol hot path P0 | Initial/Handshake/1-RTT packet encode/decode, coalesced, dispatch |
| `congestion_control_perf_test` | protocol hot path P0 | congestion-control event streams, Pacer |
| `frame_perf_test` | protocol depth P1 | Encode / Decode of 14 frame kinds, large ACK ranges |
| `qpack_perf_test` | protocol depth P1 | QPACK large-header encode/decode, dynamic table, blocked registry |
| `loss_recovery_perf_test` | protocol depth P1 | RTT update / queries, ACK burst, loss burst |
| `e2e_perf_test` | end-to-end full chain | handshake, throughput, concurrency, stability (real client ↔ server) |

Plus **non-benchmark sampling profilers** (the `add_profiler` targets):

| Executable | Purpose |
|---|---|
| `profile_decode_packets` | sample CPU hotspots of `quic::DecodePackets()` |
| `profile_qpack_encode` | sample the QPACK encoding path |
| `profile_blocked_registry` | sample QPACK Blocked Registry hotspots |
| `profile_rss_lifecycle` | observe long-connection RSS over time |

All profilers share `test/perf/tools/sampling_profiler.h` (a ~150-line SIGPROF + `backtrace(3)` implementation), paired with `test/perf/tools/resolve_stacks.py` (`addr2line` + `c++filt`) to produce Brendan-Gregg-style collapsed stacks ready for `flamegraph.pl`.

### 1.2 Per-Binary Case Inventory

The tables below list every benchmark currently registered in the repo (names exactly matching the source files, for `--benchmark_filter`):

#### `cpu_hotspot_test`

| Benchmark | Note |
|---|---|
| `BM_CpuHotspot_TlsCtxCreation` | TLS context creation |
| `BM_CpuHotspot_BufferWriteRead/{64,256,1200,4096,16384}` | Buffer read/write at typical QUIC packet sizes |
| `BM_CpuHotspot_BufferEncodeVarInt` | VarInt encoding |
| `BM_CpuHotspot_AckFrameEncode/Decode` | ACK frame encode/decode |
| `BM_CpuHotspot_StreamFrameEncode` | STREAM frame encoding |
| `BM_CpuHotspot_QpackEncode/Decode` | QPACK encode/decode |
| `BM_CpuHotspot_HuffmanEncode/Decode` | Huffman |
| `BM_CpuHotspot_PoolAllocator/{16,64,128,256}` | `Poolallocator` small-object allocation |
| `BM_CpuHotspot_BlockPoolAllocator/{1024,4096,16384}` | `BlockMemoryPool` large blocks |
| `BM_CpuHotspot_StdMalloc/{16,256,4096}` | control group |
| `BM_CpuHotspot_PacketProcessingSimulation` | full per-packet processing-path simulation |
| `BM_CpuHotspot_MultiThreadBufferAlloc` | multi-threaded Buffer allocation |

#### `memory_baseline_test`

| Benchmark | Scope |
|---|---|
| `BM_MemoryBaseline_BufferFootprint/{1K,4K,16K,64K}` | Buffer footprint |
| `BM_MemoryBaseline_PoolAllocatorOverhead` | Pool allocation overhead |
| `BM_MemoryBaseline_BlockPoolEfficiency/{1K,4K,16K}` | BlockPool efficiency |
| `BM_MemoryBaseline_ManySmallBuffers` | many small Buffers |
| `BM_MemoryBaseline_AllocFreeStability` | repeated alloc/free stability (10K rounds) |
| `BM_MemoryBaseline_BufferChainGrowth/{4K,16K,64K,256K}` | Buffer chain growth |
| `BM_MemoryBaseline_SharedPtrOverhead` | `shared_ptr` overhead |
| `BM_MemoryBaseline_PoolReleaseHalf` | `ReleaseHalf()` behavior |

#### `memory_pool_efficiency_test`

| Benchmark | Description |
|---|---|
| `BM_PoolEfficiency_PoolallocatorVsMalloc_{Pool,Malloc}` | small-object comparison |
| `BM_PoolEfficiency_BlockPoolVsMalloc_{Pool,Malloc}` | large-block comparison |
| `BM_PoolEfficiency_MixedWorkload_{Pool,Malloc}` | mixed workload |
| `BM_PoolEfficiency_BufferPerPacket/{10,100,1000}` | per-packet Buffers |
| `BM_PoolEfficiency_PoolExpansion/{10,50,200,500}` | pool expansion |
| `BM_PoolEfficiency_MultiThreadContention` | multi-thread lock contention |
| `BM_PoolEfficiency_RealWorldSizeDistribution` | realistic size distribution |
| `BM_PoolEfficiency_Normalallocator` | plain `allocator` control |

#### `crypto_perf_test`

Three AEAD suites (AES-128-GCM / AES-256-GCM / ChaCha20-Poly1305) × four operations (`EncryptPacket` / `DecryptPacket` / `EncryptHeader` / `DecryptHeader`) = 12 cases, plus:

- `BM_Hkdf_Expand_Sha256_32`
- `BM_Hkdf_Expand_Sha384_48`

#### `packet_perf_test`

| Benchmark | Scenario |
|---|---|
| `BM_Packet_InitPacket_Encode{NoCrypto,WithCrypto}` | Initial encoding |
| `BM_Packet_InitPacket_Decode{NoCrypto,WithCrypto}` | Initial decoding |
| `BM_Packet_HandshakePacket_EncodeNoCrypto` | Handshake encoding |
| `BM_Packet_Rtt1Packet_EncodeNoCrypto` | 1-RTT encoding |
| `BM_Packet_PacketNumber_Encode` / `DecodeTruncated` | packet-number encode/decode |
| `BM_Packet_Coalesced_Decode` | coalesced-packet decoding |
| `BM_Packet_SinglePacket_DecodeViaDispatch` / `_NoAlloc` | dispatch-layer decoding |

#### `congestion_control_perf_test`

| Benchmark | Description |
|---|---|
| `BM_Cc_OnPacketSent` | send event |
| `BM_Cc_EventStream` | event stream |
| `BM_Cc_CanSend` | send decision |
| `BM_Pacer_CanSend_TimeUntilSend` | Pacer |

#### `frame_perf_test`

Nine frame kinds (Crypto / ResetStream / StopSending / MaxStreamData / NewConnectionId / PathChallenge / ConnectionClose / NewToken / HandshakeDone), one Encode/Decode pair each, plus:

- `BM_Frame_AckFrame_ManyRanges_Encode/Decode` — large-range ACK

#### `qpack_perf_test`

| Benchmark | Description |
|---|---|
| `BM_Qpack_Encode_LargeHeaders` | large-header encoding |
| `BM_Qpack_Decode_LargeHeaders` | large-header decoding |
| `BM_Qpack_DynamicTable_Insert` | dynamic-table insertion |
| `BM_Qpack_DynamicTable_Find` | dynamic-table lookup |
| `BM_Qpack_BlockedRegistry_AddAck` | Blocked Registry |

#### `loss_recovery_perf_test`

| Benchmark | Description |
|---|---|
| `BM_Recovery_RttUpdate` | RTT update |
| `BM_Recovery_RttGetters` | RTT accessors |
| `BM_Recovery_AckBurst/{N}` | ACK burst (N = number of ranges) |
| `BM_Recovery_LossBurst/{N}` | loss burst |

#### `e2e_perf_test`

Real client ↔ server loopback full-chain tests, in four scenario families:

| Benchmark | Parameter | Iterations | Description |
|---|---|---|---|
| `BM_E2E_Handshake_NewConnection` | — | 10 | fresh client + HTTP/3 request each round |
| `BM_E2E_Handshake_Burst/{5,10}` | concurrent connections | 5 | N new handshakes started simultaneously |
| `BM_E2E_Throughput_Download` | — | 10 | 1 MB download (reused connection) |
| `BM_E2E_Throughput_Upload/{1K,64K,256K}` | body size | 10 | upload (reused connection) |
| `BM_E2E_Throughput_Sequential/{10,50}` | request count | 5 | sequential requests on one connection |
| `BM_E2E_Concurrency_MultiStream/{5,10,20}` | concurrent streams | 5 | concurrent streams on one connection |
| `BM_E2E_Concurrency_MultiClient/{2,5,10}` | concurrent clients | 5 | multiple concurrent clients |
| `BM_E2E_Stability_SustainedLoad/{5,10}` | sustained seconds | 1 | sustained load |
| `BM_E2E_Stability_ConnectDisconnect/{10,20}` | loop count | 1 | repeated connect/disconnect |

> On the `Iterations` setting: `e2e_perf_test` uses a **custom `main()`** that calls `SetDefaultInitialRtt(100)` before starting, lowering the pre-handshake PTO from 775 ms to ~100 ms to remove run-to-run jitter caused by first-packet loss on loopback (details in `docs/internal/perf_e2e_analysis.md` §6).

---

## 2. Building

Performance tests are controlled by the top-level CMake switch `ENABLE_PERF_TESTS` (default **ON**):

```bash
# 1) Full Release/RelWithDebInfo build (recommended for performance numbers)
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j

# 2) Disable only non-essential modules to speed up compilation
cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DENABLE_TESTING=OFF -DENABLE_FUZZING=OFF -DENABLE_INTEROP=OFF \
    -DENABLE_BENCHMARKS=OFF -DENABLE_CC_SIMULATOR=OFF
cmake --build build-perf -j

# 3) ASan build (verifies lifetime correctness; numbers NOT usable as baselines)
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g -O1" \
    -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g -O1" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
cmake --build build-asan -j
```

The perf targets' compile flags (see `test/perf/CMakeLists.txt::add_perf_test`) are fixed at:

```
-O2 -g -fno-omit-frame-pointer
-DQUICX_ENABLE_BENCHMARKS
```

These are **independent of** the top-level `CMAKE_BUILD_TYPE`, ensuring profiling friendliness (frame pointers + symbols) while staying close to production optimization levels.

All executables land in:

```
build/bin/perf/
├── cpu_hotspot_test
├── memory_baseline_test
├── memory_pool_efficiency_test
├── crypto_perf_test
├── packet_perf_test
├── congestion_control_perf_test
├── frame_perf_test
├── qpack_perf_test
├── loss_recovery_perf_test
├── e2e_perf_test
├── profile_decode_packets
├── profile_qpack_encode
├── profile_blocked_registry
└── profile_rss_lifecycle
```

---

## 3. Running

### 3.1 Most Common: Run One Binary Directly

```bash
# Run all benchmarks
./build/bin/perf/cpu_hotspot_test

# Run a specific benchmark (regex filter)
./build/bin/perf/cpu_hotspot_test --benchmark_filter=BufferWriteRead

# Run only ACK-frame-related cases
./build/bin/perf/frame_perf_test --benchmark_filter='AckFrame'

# Enforce a minimum duration (3 seconds per case for stability)
./build/bin/perf/crypto_perf_test --benchmark_min_time=3s

# Repeat 5 times, report mean/median/variance
./build/bin/perf/packet_perf_test --benchmark_repetitions=5 \
    --benchmark_report_aggregates_only=true
```

### 3.2 E2E Test Caveats

- **Occupies UDP port 19501** (`BM_E2E_Handshake_NewConnection`) and 19502+ (the other e2e scenarios); loopback permission required.
- **Run single-instance**: E2E starts a server thread and a client thread simultaneously; do not run it concurrently with other performance processes.
- **The first run is slower**: cold-start PTO / socket-buffer warm-up make the first iteration slow; already averaged out via `Iterations(10)`.

```bash
# Run the full e2e (~3–5 minutes)
./build/bin/perf/e2e_perf_test

# Run only throughput scenarios
./build/bin/perf/e2e_perf_test --benchmark_filter='Throughput'

# Run only the 5-second sustained-load variant
./build/bin/perf/e2e_perf_test --benchmark_filter='SustainedLoad/5'
```

### 3.3 JSON Export (for regression / comparison)

```bash
mkdir -p perf_results
./build/bin/perf/cpu_hotspot_test \
    --benchmark_format=json \
    --benchmark_out=perf_results/cpu_hotspot.json

# Compare two JSON runs
python3 -c "
import json
a = json.load(open('perf_results/before.json'))
b = json.load(open('perf_results/after.json'))
for x, y in zip(a['benchmarks'], b['benchmarks']):
    d = (y['real_time'] - x['real_time']) / x['real_time'] * 100
    print(f\"{x['name']:<50s} {d:+6.1f}%\")
"
```

Or use Google Benchmark's official `compare.py`:

```bash
python3 third/benchmark/tools/compare.py benchmarks \
    perf_results/before.json perf_results/after.json
```

### 3.4 Running under ASan / LSan

ASan verifies **zero reference cycles, zero use-after-free, zero heap-use-after-free** (per the lifetime conventions of [`../design/ownership_and_memory.md`](../design/ownership_and_memory.md)):

```bash
# Run the full e2e with ASan-built artifacts
export LSAN_OPTIONS="suppressions=$PWD/test/perf/lsan_suppressions.txt"
./build-asan/bin/perf/e2e_perf_test \
    --benchmark_filter='Handshake_NewConnection|MultiStream|MultiClient|ConnectDisconnect|SustainedLoad|Throughput_Download'
```

> `lsan_suppressions.txt` suppresses only `BlockMemoryPool::Expansion` / `PoolLargeMalloc` — the global memory pool's **intentional** long lifetime, not a reference cycle.

### 3.5 Sampling Profiler + Flame Graphs

```bash
# 1) Run the sampler (default 997 Hz, outputs /tmp/decode_stacks.raw and .maps)
./build/bin/perf/profile_decode_packets

# 2) Offline symbolization → collapsed stacks
python3 test/perf/tools/resolve_stacks.py \
    /tmp/decode_stacks.raw \
    /tmp/decode_stacks.raw.maps \
    --out /tmp/decode_stacks

# 3) Generate the flame graph (requires Brendan Gregg's flamegraph.pl)
flamegraph.pl /tmp/decode_stacks.collapsed > /tmp/decode_stacks.svg
```

The four profilers share the same trigger conditions; they differ only in the **hot function being sampled** (see the top comments of `test/perf/tools/profile_*.cpp`).

---

## 4. Reference Results

This section gives "reasonable value ranges" for quickly judging whether a run deviates from the norm. All data comes from `docs/en/reports/performance_baseline.md` (macOS ARM64 / Apple M3 Pro / Clang -O2) and `docs/internal/perf_e2e_analysis.md` (Linux Debug loopback).

### 4.1 CPU Micro-benchmarks (RelWithDebInfo, ARM64 M3 Pro)

| Item | Typical Value |
|---|---|
| TLS Context creation | ~16 ns |
| Buffer RW 1200 B | ~4.4 μs (~517 MiB/s) |
| VarInt encoding | ~132 ns (22.7 M items/s) |
| ACK encode/decode | ~4.3 / 4.5 μs |
| Stream encoding | ~9.0 μs |
| QPACK encode (9 fields) | ~6.2 μs (1.46 M headers/s) |
| QPACK decode (5 fields) | ~4.8 μs |
| Huffman decode | ~36 ns (317 MiB/s) |
| `Poolallocator` 16–256 B | ~2.1 ns (6–13× faster than malloc) |
| `BlockMemoryPool` 1K–16K | ~7.1 ns (~1.5× faster than malloc) |
| Packet-processing simulation | ~168 ns (theoretical 5.95 M pps) |

### 4.2 Memory Baseline

| Item | Typical Value |
|---|---|
| Buffer 1K/4K/16K creation | ~5.3 μs |
| BlockPool 100 blocks (any size) | pool steady at 28 blocks; `ReleaseHalf` reclaims ~50% |
| RSS growth after 10K alloc/free rounds | **0 KB** (no leak) |
| Buffer Chain write throughput | 4K=2.47 GiB/s → 256K=14.6 GiB/s |
| Per-packet Buffer lifetime | 159 ns (linearly scalable) |

### 4.3 E2E (Linux Debug build / loopback, steady state)

> ⚠️ Debug-build absolute numbers are 2–10× slower than Release; trends are valid, but don't compare the values against production.

| Scenario | Steady-State Reference |
|---|---|
| MultiStream, single connection | **~1300 req/s (7.7 ms p50)** |
| Sequential, single connection | **~196 req/s** |
| Download 1 MB (reused connection) | **~9.6 MiB/s** |
| Upload 256 KB (reused connection) | **~11.6 MiB/s** |
| SustainedLoad | 196 req/s, 0 failures |
| Single-connection RSS (long run) | ~100 KB, **stable** |

### 4.4 ASan / Lifetime Verification

After the Exclusive Ownership refactor, repeated 10–20 round ASan runs of the following scenarios all pass — **zero reference cycles / zero UAF / zero heap-UAF**:

| Benchmark | Verified Rounds |
|---|---|
| `BM_E2E_Handshake_NewConnection` | 10/10 |
| `BM_E2E_Throughput_Download` | pass |
| `BM_E2E_Concurrency_MultiStream/{5,10,20}` | 100% |
| `BM_E2E_Concurrency_MultiClient/{2,5,10}` | 100% |
| `BM_E2E_Stability_ConnectDisconnect/{10,20}` | pass |
| `BM_E2E_Stability_SustainedLoad` | pass |

Known ASan findings (**unrelated** to the lifetime model; concurrency races / bugs in the interface itself):

- `BM_E2E_Handshake_Burst/10` — `InitPacket::Encode` use-after-free (concurrency race)
- `BM_E2E_Throughput_Upload` — `MultiBlockBuffer::Write` memcpy overlap

These two are currently skipped in the ASan regression set but run as usual in Release number testing.

---

## 5. How to Read Benchmark Reports

Typical Google Benchmark output:

```
------------------------------------------------------------------------------
Benchmark                                    Time             CPU   Iterations UserCounters...
------------------------------------------------------------------------------
BM_Qpack_Encode_LargeHeaders              6193 ns         6190 ns       113012 items_per_second=1.46M/s
BM_E2E_Concurrency_MultiStream/10         7.71 ms         0.40 ms           5 items_per_second=1298/s
```

- **Time** — real elapsed time (look at this for `UseRealTime()` scenarios; E2E defaults to it)
- **CPU** — CPU time consumed by the benchmark thread; in E2E it being far below Time is normal (waiting on I/O)
- **Iterations** — actual iteration count (determined by `--benchmark_min_time` or explicit `Iterations()`)
- **items_per_second / bytes_per_second** — computed from `state.counters`; the most important throughput metrics

**Recommended regression thresholds** (from `docs/en/reports/performance_baseline.md` §4):

| Category | Regression Threshold |
|---|---|
| Default | > 15% slower |
| Critical paths (packet processing, frame encode/decode) | > 10% slower |
| Allocators | > 20% slower (sensitive to external factors) |

---

## 6. Recommended Workflow

**Daily changes** → run only the affected component, e.g.:

```bash
./build/bin/perf/packet_perf_test --benchmark_filter=Decode
```

**Before merging a PR** → run the full component set + key E2E scenarios:

```bash
for bin in cpu_hotspot_test memory_baseline_test memory_pool_efficiency_test \
           crypto_perf_test packet_perf_test frame_perf_test \
           qpack_perf_test loss_recovery_perf_test congestion_control_perf_test; do
    ./build/bin/perf/$bin --benchmark_format=json \
        --benchmark_out=perf_results/$bin.json
done

./build/bin/perf/e2e_perf_test \
    --benchmark_filter='Handshake_NewConnection|MultiStream|Throughput_Download' \
    --benchmark_format=json --benchmark_out=perf_results/e2e.json
```

**On discovering a performance regression** → run the sampling profiler and read the flame graph:

```bash
./build/bin/perf/profile_decode_packets
python3 test/perf/tools/resolve_stacks.py \
    /tmp/decode_stacks.raw /tmp/decode_stacks.raw.maps \
    --out /tmp/decode_stacks
flamegraph.pl /tmp/decode_stacks.collapsed > /tmp/decode_stacks.svg
```

**Lifetime changes** → re-run the key e2e scenarios with an ASan build (§3.4).

---

## 7. FAQ

**Q1: A single E2E run is slow — normal?**
The first handshake costs ~100 ms due to cold-start PTO (already reduced from 775 ms); if you see handshake times > 1 s, check whether other processes are hogging loopback bandwidth. Background in `docs/internal/perf_e2e_analysis.md`.

**Q2: Benchmarks run much slower than the documented values**
Check these four:

1. Is it a Release/RelWithDebInfo build (Debug is 2–10× slower);
2. Is ASan/TSan enabled (each 2–3× slower);
3. Is the CPU frequency governor set to `performance` (Linux);
4. Competing processes? E2E is scheduling-sensitive.

**Q3: ASan reports a BlockMemoryPool leak**
That's the global pool's long lifetime; suppress via `test/perf/lsan_suppressions.txt`:
```bash
export LSAN_OPTIONS="suppressions=$PWD/test/perf/lsan_suppressions.txt"
```

**Q4: I want to add a benchmark**
1. Add a `.cpp` under `test/perf/` (new file or into an existing one);
2. Call `add_perf_test(my_test my_test.cpp)` in `test/perf/CMakeLists.txt`;
3. Follow the naming prefix: `BM_<Component>_<Scenario>`;
4. Units: `benchmark::kNanosecond` for sub-millisecond, `kMillisecond` for E2E;
5. Throughput via `state.counters["items_per_second"] = ...` or `SetBytesProcessed()`.
