# quicX 性能基准线文档

> **日期**: 2026-03-20
> **平台**: macOS ARM64 (Apple Silicon M3 Pro, 14 cores)
> **编译**: CMake RelWithDebInfo, Clang, -O2 -g
> **框架**: Google Benchmark v1.8.3

---

## 1. CPU 热点基准线

### 1.1 TLS / 加密操作

| 基准测试 | 时间 | 吞吐量 | 说明 |
|---------|------|--------|------|
| TlsCtxCreation | ~16 ns | — | TLS 上下文对象创建（轻量） |

### 1.2 Buffer 操作（数据处理路径）

| 基准测试 | 负载大小 | 时间 | 吞吐量 |
|---------|---------|------|--------|
| BufferWriteRead | 64 B | 4.42 μs | 27.6 MiB/s |
| BufferWriteRead | 256 B | 5.34 μs | 91.5 MiB/s |
| BufferWriteRead | 1200 B | 4.43 μs | 516.7 MiB/s |
| BufferWriteRead | 4096 B | 4.62 μs | 1.65 GiB/s |
| BufferWriteRead | 16384 B | 19.3 μs | 1.58 GiB/s |
| BufferEncodeVarInt | — | 132 ns | 22.7M items/s |

**分析**: Buffer 操作对于典型 QUIC 包大小（1200B）表现优秀，吞吐量超过 500 MiB/s。

### 1.3 帧编解码

| 基准测试 | 时间 | 吞吐量 |
|---------|------|--------|
| AckFrameEncode | 4.33 μs | 231K frames/s |
| AckFrameDecode | 4.54 μs | 220K frames/s |
| StreamFrameEncode | 9.05 μs | 110K frames/s |

**分析**: ACK 帧编解码约 4.4μs，对于 QUIC 协议的性能要求完全满足。

### 1.4 QPACK 头部压缩

| 基准测试 | 时间 | 吞吐量 |
|---------|------|--------|
| QpackEncode (9 headers) | 6.19 μs | 1.46M headers/s |
| QpackDecode (5 headers) | 4.82 μs | 1.04M headers/s |
| HuffmanEncode (15 chars) | 139 ns | 103 MiB/s |
| HuffmanDecode (11 bytes) | 36.1 ns | 317 MiB/s |

**分析**: QPACK 编码性能良好。Huffman 解码比编码快约 3x，表明解码查表效率高。

### 1.5 内存分配器对比

| 分配器 | 大小 | 时间 | 加速比 vs malloc |
|--------|------|------|-----------------|
| PoolAlloter | 16 B | 2.10 ns | **6.2x** |
| PoolAlloter | 64 B | 2.10 ns | **6.0x** |
| PoolAlloter | 128 B | 2.06 ns | **6.1x** |
| PoolAlloter | 256 B | 2.06 ns | **12.6x** |
| BlockMemoryPool | 1024 B | 7.13 ns | **1.5x** |
| BlockMemoryPool | 4096 B | 7.13 ns | **1.5x** |
| BlockMemoryPool | 16384 B | 6.99 ns | **1.6x** |
| std::malloc | 16 B | 13.0 ns | 1.0x |
| std::malloc | 256 B | 28.5 ns | 1.0x |
| std::malloc | 4096 B | 10.6 ns | 1.0x |

**关键发现**:
- **PoolAlloter 小对象（≤256B）**: 比 malloc 快 **6-13x**，仅需 ~2ns
- **BlockMemoryPool 大块（1K-16K）**: 比 malloc 快 **1.5x**，~7ns
- 自定义分配器对 QUIC 包处理的内存管理有显著优势

### 1.6 包处理模拟

| 基准测试 | 时间 | 吞吐量 |
|---------|------|--------|
| PacketProcessingSimulation | 0.168 μs | 6.67 GiB/s |

**分析**: 完整的每包处理路径（分配 + 写入 + 解析头部 + 读取帧数据）仅 168 ns，理论上支持 5.95M 包/秒。

---

## 2. 内存使用基准线

### 2.1 Buffer 内存占用

| Buffer 容量 | 创建时间 | Chunk 数量 |
|------------|---------|-----------|
| 1 KiB | 5.34 μs | 1 |
| 4 KiB | 5.37 μs | 1 |
| 16 KiB | 5.27 μs | 1 |
| 64 KiB | 23.8 μs | 多个 |

### 2.2 Block Memory Pool 效率

| 块大小 | 100 块分配后池大小 | 释放后池大小 | ReleaseHalf 后 |
|-------|-------------------|-------------|---------------|
| 1 KiB | 28 | 28 | 14 |
| 4 KiB | 28 | 28 | 14 |
| 16 KiB | 28 | 28 | 14 |

**分析**: BlockMemoryPool 批量分配策略效率高。ReleaseHalf 可回收约 50% 空闲内存。

### 2.3 长期内存稳定性

| 指标 | 值 |
|------|-----|
| 初始 RSS | ~108 MB |
| 10K 次分配/释放循环后 RSS | ~108 MB |
| RSS 增长 | 0 KB |

**结论**: 在重复分配/释放循环中未观察到内存增长，表明内存池没有泄漏。

### 2.4 Buffer Chain 增长

| 写入数据量 | Chunk 数量 | 写入吞吐量 |
|-----------|-----------|-----------|
| 4 KiB | 1 | 2.47 GiB/s |
| 16 KiB | 4 | 6.62 GiB/s |
| 64 KiB | 16 | 12.06 GiB/s |
| 256 KiB | 64 | 14.64 GiB/s |

**分析**: Buffer Chain 的写入吞吐量随数据量增长而提高（摊薄了对象创建开销），64 chunk 时达到 14.6 GiB/s。

---

## 3. 内存池效率分析

### 3.1 混合负载对比

| 工作模式 | PoolAlloter | std::malloc | 加速比 |
|---------|-------------|-------------|--------|
| 混合分配/释放 | 356 ns | 1108 ns | **3.1x** |

### 3.2 每包 Buffer 分配

| 包数量 | 总时间 | 平均每包 |
|--------|--------|---------|
| 10 | 1.59 μs | 159 ns |
| 100 | 15.9 μs | 159 ns |
| 1000 | 159 μs | 159 ns |

**分析**: 每包 Buffer 分配+使用+释放开销稳定在 159 ns，线性可扩展。

### 3.3 池扩展行为

| 初始容量 | 请求块数 | 最终池大小 | 释放后池大小 |
|---------|---------|-----------|------------|
| 4 块 | 10 | 2 (空闲) | 12 (返回池) |
| 4 块 | 50 | 2 | 12 |
| 4 块 | 200 | 0 | 20 |
| 4 块 | 500 | 0 | 20 |

---

## 4. 性能阈值定义

以下阈值用于 CI/CD 性能回归检测（`scripts/ci/perf_regression.sh`）：

| 指标类别 | 阈值 | 说明 |
|---------|------|------|
| **默认阈值** | 15% | 超过此值标记为回归 |
| **关键路径** | 10% | 包处理、帧编解码 |
| **分配器** | 20% | 内存分配操作（对外部因素更敏感） |

---

## 5. 优化建议

基于基准线数据的优化方向：

1. **Buffer 创建开销（~5μs）**: 考虑 Buffer 对象池，避免每包创建新 Buffer
2. **StreamFrame 编码（9μs）**: 比 AckFrame（4.3μs）慢 2x，可能有优化空间
3. **QPACK 编码（6μs/请求）**: 对于高频 HTTP/3 请求，动态表命中率是关键
4. **BlockMemoryPool 线程安全**: 多线程下有锁竞争，可考虑 per-thread pool

---

## 6. 复现说明

```bash
# 构建
cmake -B build -DENABLE_PERF_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j

# 运行 CPU 热点基准
./build/bin/perf/cpu_hotspot_test

# 运行内存基准
./build/bin/perf/memory_baseline_test

# 运行内存池效率分析
./build/bin/perf/memory_pool_efficiency_test

# 输出 JSON 报告
./build/bin/perf/cpu_hotspot_test --benchmark_format=json --benchmark_out=perf_results/cpu_hotspot.json

# 保存为性能基准线
./scripts/ci/perf_regression.sh --save-baseline

# 生成火焰图
./scripts/perf/generate_flamegraph.sh -d 30 ./build/bin/perf/cpu_hotspot_test

# 运行 ASan 内存分析
./scripts/perf/run_memory_analysis.sh -m asan ./build/bin/quicx_utest
```

---

## 附录：CMake 选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `ENABLE_PERF_TESTS` | ON | 构建性能分析测试 |
| `ENABLE_PROFILING` | OFF | 启用分析友好编译标志（-g -fno-omit-frame-pointer） |
| `ENABLE_ASAN` | OFF | 启用 AddressSanitizer |
| `ENABLE_LSAN` | OFF | 启用 LeakSanitizer |
| `ENABLE_TSAN` | OFF | 启用 ThreadSanitizer |
