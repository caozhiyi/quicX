# quicX Performance Baseline

**Platform**: macOS ARM64 (Apple Silicon M3 Pro, 14 cores)

**Build**: CMake RelWithDebInfo, Clang, -O2 -g

**Framework**: Google Benchmark v1.8.3

---

## 1. CPU Hotspot Baseline

### 1.1 TLS / Crypto Operations

| Benchmark | Time | Throughput | Note |
|---------|------|--------|------|
| TlsCtxCreation | ~16 ns | — | TLS context object creation (lightweight) |

### 1.2 Buffer Operations (Data-Processing Path)

| Benchmark | Payload Size | Time | Throughput |
|---------|---------|------|--------|
| BufferWriteRead | 64 B | 4.42 μs | 27.6 MiB/s |
| BufferWriteRead | 256 B | 5.34 μs | 91.5 MiB/s |
| BufferWriteRead | 1200 B | 4.43 μs | 516.7 MiB/s |
| BufferWriteRead | 4096 B | 4.62 μs | 1.65 GiB/s |
| BufferWriteRead | 16384 B | 19.3 μs | 1.58 GiB/s |
| BufferEncodeVarInt | — | 132 ns | 22.7M items/s |

**Analysis**: Buffer operations excel at the typical QUIC packet size (1200B), with throughput above 500 MiB/s.

### 1.3 Frame Encode/Decode

| Benchmark | Time | Throughput |
|---------|------|--------|
| AckFrameEncode | 4.33 μs | 231K frames/s |
| AckFrameDecode | 4.54 μs | 220K frames/s |
| StreamFrameEncode | 9.05 μs | 110K frames/s |

**Analysis**: ACK frame encode/decode at ~4.4μs fully satisfies QUIC protocol performance requirements.

### 1.4 QPACK Header Compression

| Benchmark | Time | Throughput |
|---------|------|--------|
| QpackEncode (9 headers) | 6.19 μs | 1.46M headers/s |
| QpackDecode (5 headers) | 4.82 μs | 1.04M headers/s |
| HuffmanEncode (15 chars) | 139 ns | 103 MiB/s |
| HuffmanDecode (11 bytes) | 36.1 ns | 317 MiB/s |

**Analysis**: QPACK encoding performs well. Huffman decoding is ~3× faster than encoding, indicating a highly efficient decode lookup table.

### 1.5 Allocator Comparison

| Allocator | Size | Time | Speedup vs malloc |
|--------|------|------|-----------------|
| Poolallocator | 16 B | 2.10 ns | **6.2x** |
| Poolallocator | 64 B | 2.10 ns | **6.0x** |
| Poolallocator | 128 B | 2.06 ns | **6.1x** |
| Poolallocator | 256 B | 2.06 ns | **12.6x** |
| BlockMemoryPool | 1024 B | 7.13 ns | **1.5x** |
| BlockMemoryPool | 4096 B | 7.13 ns | **1.5x** |
| BlockMemoryPool | 16384 B | 6.99 ns | **1.6x** |
| std::malloc | 16 B | 13.0 ns | 1.0x |
| std::malloc | 256 B | 28.5 ns | 1.0x |
| std::malloc | 4096 B | 10.6 ns | 1.0x |

**Key findings**:
- **Poolallocator small objects (≤256B)**: **6–13× faster** than malloc, at only ~2ns
- **BlockMemoryPool large blocks (1K–16K)**: **1.5× faster** than malloc, at ~7ns
- The custom allocators carry significant advantages for QUIC packet-processing memory management

### 1.6 Packet-Processing Simulation

| Benchmark | Time | Throughput |
|---------|------|--------|
| PacketProcessingSimulation | 0.168 μs | 6.67 GiB/s |

**Analysis**: The full per-packet processing path (allocate + write + parse header + read frame data) takes only 168 ns, theoretically supporting 5.95M packets/second.

---

## 2. Memory Usage Baseline

### 2.1 Buffer Memory Footprint

| Buffer Capacity | Creation Time | Chunk Count |
|------------|---------|-----------|
| 1 KiB | 5.34 μs | 1 |
| 4 KiB | 5.37 μs | 1 |
| 16 KiB | 5.27 μs | 1 |
| 64 KiB | 23.8 μs | multiple |

### 2.2 Block Memory Pool Efficiency

| Block Size | Pool Size After 100 Allocations | Pool Size After Release | After ReleaseHalf |
|-------|-------------------|-------------|---------------|
| 1 KiB | 28 | 28 | 14 |
| 4 KiB | 28 | 28 | 14 |
| 16 KiB | 28 | 28 | 14 |

**Analysis**: BlockMemoryPool's batch allocation strategy is efficient. ReleaseHalf reclaims roughly 50% of idle memory.

### 2.3 Long-Run Memory Stability

| Metric | Value |
|------|-----|
| Initial RSS | ~108 MB |
| RSS after 10K alloc/free cycles | ~108 MB |
| RSS growth | 0 KB |

**Conclusion**: No memory growth observed across repeated alloc/free cycles, indicating the memory pools don't leak.

### 2.4 Buffer Chain Growth

| Data Written | Chunk Count | Write Throughput |
|-----------|-----------|-----------|
| 4 KiB | 1 | 2.47 GiB/s |
| 16 KiB | 4 | 6.62 GiB/s |
| 64 KiB | 16 | 12.06 GiB/s |
| 256 KiB | 64 | 14.64 GiB/s |

**Analysis**: Buffer Chain write throughput rises with data volume (amortizing object-creation overhead), reaching 14.6 GiB/s at 64 chunks.

---

## 3. Memory Pool Efficiency Analysis

### 3.1 Mixed-Workload Comparison

| Workload | Poolallocator | std::malloc | Speedup |
|---------|-------------|-------------|--------|
| Mixed alloc/free | 356 ns | 1108 ns | **3.1x** |

### 3.2 Per-Packet Buffer Allocation

| Packet Count | Total Time | Per Packet |
|--------|--------|---------|
| 10 | 1.59 μs | 159 ns |
| 100 | 15.9 μs | 159 ns |
| 1000 | 159 μs | 159 ns |

**Analysis**: The per-packet Buffer allocate + use + free overhead holds steady at 159 ns, scaling linearly.

### 3.3 Pool Expansion Behavior

| Initial Capacity | Requested Blocks | Final Pool Size | Pool Size After Release |
|---------|---------|-----------|------------|
| 4 blocks | 10 | 2 (idle) | 12 (returned to pool) |
| 4 blocks | 50 | 2 | 12 |
| 4 blocks | 200 | 0 | 20 |
| 4 blocks | 500 | 0 | 20 |

---

## 4. Performance Threshold Definitions

The following thresholds serve as reference criteria for performance-regression detection (comparing deviation between two runs' JSON reports):

| Metric Category | Threshold | Note |
|---------|------|------|
| **Default threshold** | 15% | beyond this, flag as regression |
| **Critical paths** | 10% | packet processing, frame encode/decode |
| **Allocators** | 20% | memory-allocation operations (more sensitive to external factors) |

---

## 5. Optimization Suggestions

Optimization directions based on the baseline data:

1. **Buffer creation overhead (~5μs)**: consider a Buffer object pool to avoid creating a new Buffer per packet
2. **StreamFrame encoding (9μs)**: 2× slower than AckFrame (4.3μs); there may be room for optimization
3. **QPACK encoding (6μs/request)**: for high-frequency HTTP/3 requests, the dynamic-table hit rate is key
4. **BlockMemoryPool thread safety**: lock contention under multi-threading; consider a per-thread pool

---

## 6. Reproduction

```bash
# Build
cmake -B build -DENABLE_PERF_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j

# Run the CPU hotspot benchmarks
./build/bin/perf/cpu_hotspot_test

# Run the memory benchmarks
./build/bin/perf/memory_baseline_test

# Run the memory-pool efficiency analysis
./build/bin/perf/memory_pool_efficiency_test

# Output JSON reports
./build/bin/perf/cpu_hotspot_test --benchmark_format=json --benchmark_out=perf_results/cpu_hotspot.json

# Save a JSON report as the performance baseline (compare future runs against it)
./build/bin/perf/cpu_hotspot_test --benchmark_format=json --benchmark_out=baseline/cpu_hotspot.json

# Sample-profile for 30 seconds and generate a flame graph (collapsed stacks feed
# flamegraph.pl directly)
./build/bin/perf/profile_decode_packets --seconds 30 --out /tmp/decode_stacks.raw
python3 test/perf/tools/resolve_stacks.py /tmp/decode_stacks.raw

# ASan memory analysis (build separately with -DSANITIZER=asan, then run unit tests)
cmake -B build-asan -DSANITIZER=asan -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-asan -j
./build-asan/bin/quicx_utest
```

---

## Appendix: CMake Options

| Option | Default | Description |
|------|--------|------|
| `ENABLE_PERF_TESTS` | ON | build the performance tests (test/perf, including the sampling profiler tools) |
| `SANITIZER` | (empty) | one of `asan` / `ubsan` / `tsan`; enables the corresponding sanitizer build |

> Note: perf targets carry their own profiling-friendly compile flags (`-O2 -g -fno-omit-frame-pointer`); no separate profiling switch is needed.
