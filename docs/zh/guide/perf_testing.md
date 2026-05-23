# quicX 性能测试指南

本文档介绍 quicX 仓库中 **`test/perf/`** 下的全部性能基准与性能剖析工具，内容涵盖：

1. 测试集合总览与用例清单
2. 构建与运行方式
3. 常用命令模板（单项、JSON 输出、筛选、比较）
4. 参考结果与如何解读

如需了解**性能基准线**（数值大表）和**E2E 根因分析**的历史结论，请继续参考：

- `docs/zh/reports/performance_baseline.md` — 基于 macOS ARM64 / Release 的数值快照
- `docs/internal/perf_e2e_analysis.md` — Debug build e2e 场景的根因分析
- `docs/internal/perf_flamegraph_analysis.md` — 火焰图采样结果

---

## 1. 测试组织与用例清单

所有性能测试都基于 [Google Benchmark v1.8.3](https://github.com/google/benchmark)，由 `test/perf/CMakeLists.txt` 注册为独立可执行文件，输出到 `build/bin/perf/`。

### 1.1 测试矩阵

| 可执行文件 | 定位 | 主要覆盖面 |
|---|---|---|
| `cpu_hotspot_test` | 组件级 CPU 热点 | TLS / Buffer / Frame / QPACK / Pool / 包处理模拟 |
| `memory_baseline_test` | 内存基准 | Buffer 足迹、Block Pool 效率、长期稳定性、Chain 增长 |
| `memory_pool_efficiency_test` | 内存池对比 | `PoolAlloter` / `BlockMemoryPool` / `std::malloc` 多种负载 |
| `crypto_perf_test` | 协议热路径 P0 | AEAD（AES-128/256-GCM、ChaCha20-Poly1305）、HKDF |
| `packet_perf_test` | 协议热路径 P0 | Initial/Handshake/1-RTT 包编解码、coalesced、dispatch |
| `congestion_control_perf_test` | 协议热路径 P0 | 拥塞控制事件流、Pacer |
| `frame_perf_test` | 协议深度 P1 | 14 种帧的 Encode / Decode、大 ACK 范围 |
| `qpack_perf_test` | 协议深度 P1 | QPACK 大头部编解码、动态表、阻塞注册表 |
| `loss_recovery_perf_test` | 协议深度 P1 | RTT 更新 / 查询、ACK burst、Loss burst |
| `e2e_perf_test` | 端到端全链路 | 握手、吞吐、并发、稳定性（真实 client ↔ server） |

外加**非 benchmark 的采样剖析器**（`add_profiler` 目标）：

| 可执行文件 | 用途 |
|---|---|
| `profile_decode_packets` | 采样 `quic::DecodePackets()` 的 CPU 热点 |
| `profile_qpack_encode` | 采样 QPACK 编码路径 |
| `profile_blocked_registry` | 采样 QPACK Blocked Registry 热点 |
| `profile_rss_lifecycle` | 观测长连接 RSS 随时间变化 |

所有 profiler 共用 `test/perf/tools/sampling_profiler.h`（SIGPROF + `backtrace(3)` 的 ~150 行实现），配合 `test/perf/tools/resolve_stacks.py`（`addr2line` + `c++filt`）生成 Brendan Gregg 风格的 collapsed stacks，可直接喂 `flamegraph.pl`。

### 1.2 逐项用例清单

下表逐个列出当前仓库注册的 benchmark（名字与源文件完全一致，便于 `--benchmark_filter`）：

#### `cpu_hotspot_test`

| Benchmark | 备注 |
|---|---|
| `BM_CpuHotspot_TlsCtxCreation` | TLS 上下文创建 |
| `BM_CpuHotspot_BufferWriteRead/{64,256,1200,4096,16384}` | Buffer 读写，典型 QUIC 包大小 |
| `BM_CpuHotspot_BufferEncodeVarInt` | VarInt 编码 |
| `BM_CpuHotspot_AckFrameEncode/Decode` | ACK 帧编解码 |
| `BM_CpuHotspot_StreamFrameEncode` | STREAM 帧编码 |
| `BM_CpuHotspot_QpackEncode/Decode` | QPACK 编解码 |
| `BM_CpuHotspot_HuffmanEncode/Decode` | Huffman |
| `BM_CpuHotspot_PoolAllocator/{16,64,128,256}` | `PoolAlloter` 小对象分配 |
| `BM_CpuHotspot_BlockPoolAllocator/{1024,4096,16384}` | `BlockMemoryPool` 大块 |
| `BM_CpuHotspot_StdMalloc/{16,256,4096}` | 对照组 |
| `BM_CpuHotspot_PacketProcessingSimulation` | 完整每包处理路径模拟 |
| `BM_CpuHotspot_MultiThreadBufferAlloc` | 多线程 Buffer 分配 |

#### `memory_baseline_test`

| Benchmark | 范围 |
|---|---|
| `BM_MemoryBaseline_BufferFootprint/{1K,4K,16K,64K}` | Buffer 足迹 |
| `BM_MemoryBaseline_PoolAllocatorOverhead` | Pool 分配开销 |
| `BM_MemoryBaseline_BlockPoolEfficiency/{1K,4K,16K}` | BlockPool 效率 |
| `BM_MemoryBaseline_ManySmallBuffers` | 大量小 Buffer |
| `BM_MemoryBaseline_AllocFreeStability` | 反复 alloc/free 稳定性（10K 轮） |
| `BM_MemoryBaseline_BufferChainGrowth/{4K,16K,64K,256K}` | Buffer chain 增长 |
| `BM_MemoryBaseline_SharedPtrOverhead` | `shared_ptr` 开销 |
| `BM_MemoryBaseline_PoolReleaseHalf` | `ReleaseHalf()` 行为 |

#### `memory_pool_efficiency_test`

| Benchmark | 说明 |
|---|---|
| `BM_PoolEfficiency_PoolAlloterVsMalloc_{Pool,Malloc}` | 小对象对比 |
| `BM_PoolEfficiency_BlockPoolVsMalloc_{Pool,Malloc}` | 大块对比 |
| `BM_PoolEfficiency_MixedWorkload_{Pool,Malloc}` | 混合负载 |
| `BM_PoolEfficiency_BufferPerPacket/{10,100,1000}` | 每包 Buffer |
| `BM_PoolEfficiency_PoolExpansion/{10,50,200,500}` | 池扩展 |
| `BM_PoolEfficiency_MultiThreadContention` | 多线程锁争用 |
| `BM_PoolEfficiency_RealWorldSizeDistribution` | 真实 size 分布 |
| `BM_PoolEfficiency_NormalAlloter` | 常规 `Alloter` 对照 |

#### `crypto_perf_test`

三套 AEAD（AES-128-GCM / AES-256-GCM / ChaCha20-Poly1305）× 四种操作（`EncryptPacket` / `DecryptPacket` / `EncryptHeader` / `DecryptHeader`）共 12 项，加上：

- `BM_Hkdf_Expand_Sha256_32`
- `BM_Hkdf_Expand_Sha384_48`

#### `packet_perf_test`

| Benchmark | 场景 |
|---|---|
| `BM_Packet_InitPacket_Encode{NoCrypto,WithCrypto}` | Initial 编码 |
| `BM_Packet_InitPacket_Decode{NoCrypto,WithCrypto}` | Initial 解码 |
| `BM_Packet_HandshakePacket_EncodeNoCrypto` | Handshake 编码 |
| `BM_Packet_Rtt1Packet_EncodeNoCrypto` | 1-RTT 编码 |
| `BM_Packet_PacketNumber_Encode` / `DecodeTruncated` | 包号编解码 |
| `BM_Packet_Coalesced_Decode` | 合并包解码 |
| `BM_Packet_SinglePacket_DecodeViaDispatch` / `_NoAlloc` | 分发层解码 |

#### `congestion_control_perf_test`

| Benchmark | 说明 |
|---|---|
| `BM_Cc_OnPacketSent` | 发包事件 |
| `BM_Cc_EventStream` | 事件流 |
| `BM_Cc_CanSend` | 发送判定 |
| `BM_Pacer_CanSend_TimeUntilSend` | Pacer |

#### `frame_perf_test`

9 种帧（Crypto / ResetStream / StopSending / MaxStreamData / NewConnectionId / PathChallenge / ConnectionClose / NewToken / HandshakeDone）各一对 Encode/Decode，加上：

- `BM_Frame_AckFrame_ManyRanges_Encode/Decode` — 大范围 ACK

#### `qpack_perf_test`

| Benchmark | 说明 |
|---|---|
| `BM_Qpack_Encode_LargeHeaders` | 大头部编码 |
| `BM_Qpack_Decode_LargeHeaders` | 大头部解码 |
| `BM_Qpack_DynamicTable_Insert` | 动态表插入 |
| `BM_Qpack_DynamicTable_Find` | 动态表查找 |
| `BM_Qpack_BlockedRegistry_AddAck` | Blocked Registry |

#### `loss_recovery_perf_test`

| Benchmark | 说明 |
|---|---|
| `BM_Recovery_RttUpdate` | RTT 更新 |
| `BM_Recovery_RttGetters` | RTT 访问器 |
| `BM_Recovery_AckBurst/{N}` | ACK 突发（N 为范围数） |
| `BM_Recovery_LossBurst/{N}` | 丢包突发 |

#### `e2e_perf_test`

真实 client ↔ server 回环全链路测试，分四类场景：

| Benchmark | 参数 | 迭代 | 说明 |
|---|---|---|---|
| `BM_E2E_Handshake_NewConnection` | — | 10 | 每轮新 client + HTTP/3 请求 |
| `BM_E2E_Handshake_Burst/{5,10}` | 并发连接数 | 5 | 同时发起 N 个新握手 |
| `BM_E2E_Throughput_Download` | — | 10 | 1 MB 下载（复用连接） |
| `BM_E2E_Throughput_Upload/{1K,64K,256K}` | body 大小 | 10 | 上传（复用连接） |
| `BM_E2E_Throughput_Sequential/{10,50}` | 请求数 | 5 | 单连接顺序请求 |
| `BM_E2E_Concurrency_MultiStream/{5,10,20}` | 并发流数 | 5 | 单连接并发流 |
| `BM_E2E_Concurrency_MultiClient/{2,5,10}` | 并发客户端 | 5 | 多客户端并发 |
| `BM_E2E_Stability_SustainedLoad/{5,10}` | 持续秒数 | 1 | 持续加压 |
| `BM_E2E_Stability_ConnectDisconnect/{10,20}` | 循环次数 | 1 | 反复连断 |

> 关于 `Iterations` 设置：`e2e_perf_test` **自定义 `main()`**，启动前调用 `SetDefaultInitialRtt(100)` 把 pre-handshake PTO 从 775 ms 降到 ~100 ms，以消除回环下首包丢失导致的 run-to-run 抖动（详见 `docs/internal/perf_e2e_analysis.md` §6）。

---

## 2. 构建

性能测试由顶层 CMake 的 `ENABLE_PERF_TESTS`（默认 **ON**）开关控制：

```bash
# 1) 全量 Release/RelWithDebInfo 构建（推荐用于性能数值）
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j

# 2) 仅关闭非必要模块，加速编译
cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DENABLE_TESTING=OFF -DENABLE_FUZZING=OFF -DENABLE_INTEROP=OFF \
    -DENABLE_BENCHMARKS=OFF -DENABLE_CC_SIMULATOR=OFF
cmake --build build-perf -j

# 3) ASan 构建（验证生命周期正确性；数值不可用于基准）
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g -O1" \
    -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g -O1" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
cmake --build build-asan -j
```

perf 目标编译参数（见 `test/perf/CMakeLists.txt::add_perf_test`）固定使用：

```
-O2 -g -fno-omit-frame-pointer
-DQUICX_ENABLE_BENCHMARKS
```

这些参数**独立于**顶层的 `CMAKE_BUILD_TYPE`，确保 profiling 友好（有帧指针 + 符号），同时优化级别足够接近生产。

所有可执行产物集中在：

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

## 3. 运行方式

### 3.1 最常用：直接跑一个二进制

```bash
# 跑全部 benchmark
./build/bin/perf/cpu_hotspot_test

# 跑指定 benchmark（正则过滤）
./build/bin/perf/cpu_hotspot_test --benchmark_filter=BufferWriteRead

# 只跑 ACK 帧相关
./build/bin/perf/frame_perf_test --benchmark_filter='AckFrame'

# 限制最短时长（单项跑到 3 秒稳定）
./build/bin/perf/crypto_perf_test --benchmark_min_time=3s

# 重复 5 次，报告均值/中位数/方差
./build/bin/perf/packet_perf_test --benchmark_repetitions=5 \
    --benchmark_report_aggregates_only=true
```

### 3.2 E2E 测试注意事项

- **占用 UDP 端口 19501**（`BM_E2E_Handshake_NewConnection`）、19502+（其他 e2e 场景）；需要回环权限。
- **单线程运行**：E2E 同时起 server 线程与 client 线程，不要与其他性能进程并发跑。
- **首次运行较慢**：cold-start PTO / socket buffer 预热导致第一次迭代偏慢，已通过 `Iterations(10)` 平均化。

```bash
# 跑完整 e2e（约 3–5 分钟）
./build/bin/perf/e2e_perf_test

# 只跑吞吐类场景
./build/bin/perf/e2e_perf_test --benchmark_filter='Throughput'

# 只跑一次 sustained load 5 秒版
./build/bin/perf/e2e_perf_test --benchmark_filter='SustainedLoad/5'
```

### 3.3 JSON 导出（用于回归 / 对比）

```bash
mkdir -p perf_results
./build/bin/perf/cpu_hotspot_test \
    --benchmark_format=json \
    --benchmark_out=perf_results/cpu_hotspot.json

# 对比两次 JSON
python3 -c "
import json
a = json.load(open('perf_results/before.json'))
b = json.load(open('perf_results/after.json'))
for x, y in zip(a['benchmarks'], b['benchmarks']):
    d = (y['real_time'] - x['real_time']) / x['real_time'] * 100
    print(f\"{x['name']:<50s} {d:+6.1f}%\")
"
```

或使用 Google Benchmark 官方 `compare.py`：

```bash
python3 third/benchmark/tools/compare.py benchmarks \
    perf_results/before.json perf_results/after.json
```

### 3.4 ASan / LSan 运行

ASan 用于验证**循环引用、use-after-free、heap-use-after-free 零发生**（对应 `docs/zh/design/ownership_and_memory.md` 的生命周期约定）：

```bash
# 用 ASan 构建产物跑完整 e2e
export LSAN_OPTIONS="suppressions=$PWD/test/perf/lsan_suppressions.txt"
./build-asan/bin/perf/e2e_perf_test \
    --benchmark_filter='Handshake_NewConnection|MultiStream|MultiClient|ConnectDisconnect|SustainedLoad|Throughput_Download'
```

> `lsan_suppressions.txt` 仅屏蔽 `BlockMemoryPool::Expansion` / `PoolLargeMalloc` —— 那是全局内存池的**有意**长生命周期，非环引用。

### 3.5 采样 Profiler + 火焰图

```bash
# 1) 跑采样器（默认 997 Hz，输出 /tmp/decode_stacks.raw 与 .maps）
./build/bin/perf/profile_decode_packets

# 2) 离线符号化 → collapsed stacks
python3 test/perf/tools/resolve_stacks.py \
    /tmp/decode_stacks.raw \
    /tmp/decode_stacks.raw.maps \
    --out /tmp/decode_stacks

# 3) 生成火焰图（需要 Brendan Gregg 的 flamegraph.pl）
flamegraph.pl /tmp/decode_stacks.collapsed > /tmp/decode_stacks.svg
```

四个 profiler 的触发条件一致，差别仅在**被采样的热函数**（见 `test/perf/tools/profile_*.cpp` 顶部注释）。

---

## 4. 参考结果

本节给出"合理取值区间"，用于快速判断一次 run 是否偏离常态。所有数据来自 `docs/zh/reports/performance_baseline.md`（macOS ARM64 / Apple M3 Pro / Clang -O2）与 `docs/internal/perf_e2e_analysis.md`（Linux Debug loopback）。

### 4.1 CPU 微基准（RelWithDebInfo，ARM64 M3 Pro）

| 项 | 典型值 |
|---|---|
| TLS Context 创建 | ~16 ns |
| Buffer RW 1200 B | ~4.4 μs（~517 MiB/s） |
| VarInt 编码 | ~132 ns（22.7 M items/s） |
| ACK 编/解 | ~4.3 / 4.5 μs |
| Stream 编码 | ~9.0 μs |
| QPACK 编（9 字段）| ~6.2 μs（1.46 M headers/s） |
| QPACK 解（5 字段）| ~4.8 μs |
| Huffman 解 | ~36 ns（317 MiB/s） |
| `PoolAlloter` 16–256 B | ~2.1 ns（比 malloc 快 **6–13×**） |
| `BlockMemoryPool` 1K–16K | ~7.1 ns（比 malloc 快 **~1.5×**） |
| Packet Processing 模拟 | ~168 ns（理论 5.95 M pps） |

### 4.2 内存基线

| 项 | 典型值 |
|---|---|
| Buffer 1K/4K/16K 创建 | ~5.3 μs |
| BlockPool 100 块（任意大小）| 池稳定 28 块；`ReleaseHalf` 回收 ~50% |
| 10K 轮 alloc/free 后 RSS 增长 | **0 KB**（无泄漏） |
| Buffer Chain 写吞吐 | 4K=2.47 GiB/s → 256K=14.6 GiB/s |
| 每包 Buffer 生命周期 | 159 ns（线性可扩展） |

### 4.3 E2E（Linux Debug build / loopback，稳态）

> ⚠️ Debug build 绝对数值比 Release 慢 2–10×；趋势有效，数值不要横向对标生产。

| 场景 | 稳态参考 |
|---|---|
| MultiStream，单连接 | **~1300 req/s（7.7 ms p50）** |
| Sequential，单连接 | **~196 req/s** |
| Download 1 MB（复用连接） | **~9.6 MiB/s** |
| Upload 256 KB（复用连接） | **~11.6 MiB/s** |
| SustainedLoad | 196 req/s，0 失败 |
| 单连接 RSS（长运行）| ~100 KB，**稳定** |

### 4.4 ASan / 生命周期验证

Exclusive Ownership 重构完成后（2026-05），在 ASan 下重复 10–20 轮运行以下场景均通过，**零循环引用 / 零 UAF / 零 heap-UAF**：

| Benchmark | 验证轮次 |
|---|---|
| `BM_E2E_Handshake_NewConnection` | 10/10 |
| `BM_E2E_Throughput_Download` | 通过 |
| `BM_E2E_Concurrency_MultiStream/{5,10,20}` | 100% |
| `BM_E2E_Concurrency_MultiClient/{2,5,10}` | 100% |
| `BM_E2E_Stability_ConnectDisconnect/{10,20}` | 通过 |
| `BM_E2E_Stability_SustainedLoad` | 通过 |

已知的 ASan 报点（与生命周期模型**无关**，属并发竞态/接口自身 bug）：

- `BM_E2E_Handshake_Burst/10` — `InitPacket::Encode` use-after-free（并发竞态）
- `BM_E2E_Throughput_Upload` — `MultiBlockBuffer::Write` memcpy overlap

这两项目前从 ASan 回归集合中跳过，但在 Release 数值测试中照常运行。

---

## 5. 如何读基准报告

Google Benchmark 典型输出：

```
------------------------------------------------------------------------------
Benchmark                                    Time             CPU   Iterations UserCounters...
------------------------------------------------------------------------------
BM_Qpack_Encode_LargeHeaders              6193 ns         6190 ns       113012 items_per_second=1.46M/s
BM_E2E_Concurrency_MultiStream/10         7.71 ms         0.40 ms           5 items_per_second=1298/s
```

- **Time** — 真实耗时（`UseRealTime()` 场景看这个，E2E 默认如此）
- **CPU**  — 被 benchmark 线程占用的 CPU 时间；E2E 里远小于 Time 属正常（等待 I/O）
- **Iterations** — 实际迭代次数（由 `--benchmark_min_time` 或显式 `Iterations()` 决定）
- **items_per_second / bytes_per_second** — 由 `state.counters` 计算，是解读吞吐最重要的指标

**回归判定建议阈值**（来自 `docs/zh/reports/performance_baseline.md` §4）：

| 类别 | 回归阈值 |
|---|---|
| 默认 | > 15% 变慢 |
| 关键路径（包处理、帧编解码） | > 10% 变慢 |
| 分配器 | > 20% 变慢（对外部因素敏感） |

---

## 6. 推荐工作流

**日常改动** → 只跑受影响组件，比如：

```bash
./build/bin/perf/packet_perf_test --benchmark_filter=Decode
```

**PR 合入前** → 跑完整组件集 + E2E 关键场景：

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

**发现性能回归** → 跑采样 profiler 看火焰图：

```bash
./build/bin/perf/profile_decode_packets
python3 test/perf/tools/resolve_stacks.py \
    /tmp/decode_stacks.raw /tmp/decode_stacks.raw.maps \
    --out /tmp/decode_stacks
flamegraph.pl /tmp/decode_stacks.collapsed > /tmp/decode_stacks.svg
```

**生命周期改动** → 改用 ASan build 重跑 e2e 关键场景（§ 3.4）。

---

## 7. 常见问题

**Q1：E2E 测试单次跑很慢，正常吗？**
首次握手会因 cold-start PTO 耗 ~100 ms（已从 775 ms 降下来）；如果看到 >1 s 的握手耗时，检查是否其他进程占用回环带宽。背景详见 `docs/internal/perf_e2e_analysis.md`。

**Q2：benchmark 跑得比文档值慢很多**
核对这四项：

1. 是否使用 Release/RelWithDebInfo 构建（Debug 慢 2–10×）；
2. 是否启用了 ASan/TSan（各自慢 2–3×）；
3. CPU 频率调速器是否为 `performance`（Linux）；
4. 其他进程竞争？E2E 对调度敏感。

**Q3：ASan 报 BlockMemoryPool 泄漏**
那是全局池的长生命周期，通过 `test/perf/lsan_suppressions.txt` 抑制：
```bash
export LSAN_OPTIONS="suppressions=$PWD/test/perf/lsan_suppressions.txt"
```

**Q4：想新增一个 benchmark**
1. 在 `test/perf/` 下加 `.cpp`（新建文件或加进已有的）；
2. 在 `test/perf/CMakeLists.txt` 里调用 `add_perf_test(my_test my_test.cpp)`；
3. 遵循命名前缀：`BM_<Component>_<Scenario>`；
4. 单位：亚毫秒用 `benchmark::kNanosecond`，E2E 用 `kMillisecond`；
5. 吞吐用 `state.counters["items_per_second"] = ...` 或 `SetBytesProcessed()`。

---

*最后更新：2026-05。本文档与 `test/perf/CMakeLists.txt` 注册项一一对应；若新增/删除 benchmark，请同步更新 §1.2。*
