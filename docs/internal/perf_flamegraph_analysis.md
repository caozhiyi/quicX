# quicX `test/perf` 火焰图级性能分析与修复记录

**日期**：2026-04-30
**范围**：`test/perf/packet_perf_test.cpp` 中的 P0/P1 新增 benchmark
**工具**：自研 SIGPROF 采样 profiler (`test/perf/tools/sampling_profiler.h`) + `addr2line` 离线符号化 (`test/perf/tools/resolve_stacks.py`)

---

## TL;DR

- **现象**：`BM_Packet_SinglePacket_DecodeViaDispatch` 跑出 **~65 µs/call**，比同 payload 的手工路径 `InitPacket::DecodeNoCrypto` (~330 ns) 慢 **~195×**。
- **源码阅读**无法定位根因（没有循环，没有 syscall，没有加锁）；仅在 `LongHeader::EncodeHeader` 里多了一次 var-length 处理。这种量级差距不可能来自几个字节的差异。
- **加采样 profiler 后**，真相大白：benchmark 本身造的 wire bytes **没有 `SetVersion()`**（`LongHeader::version_` 默认为 0），`DecodePackets` 把它识别为 RFC 9000 §17.2.1 的 Version Negotiation 包，走到 `VersionNegotiationPacket::DecodeWithoutCrypto`。该函数对每 4 字节的"候选版本"各打一条 **`LOG_INFO`**（默认 log level 不过滤），于是整个热循环变成了 `snprintf` + `localtime`/`__tz_convert` 的 libc 格式化开销。
- **修复**：benchmark 与 profiler driver 都补上 `SetVersion(GetDefaultVersion())`。
- **结果**：延迟从 **65 µs → 418 ns**（**~155× 加速**），`DecodePackets` 的调度开销降到**约 89 ns**（= `418 ns - 293 ns`），符合预期。

---

## 1. 为什么需要火焰图

第一轮源码走读给出的结论是："dispatch 层会多走一次 `HeaderFlag::DecodeFlag` + 一次 `FixedDecodeUint32` + 一次 `make_shared<InitPacket>`，顶多几十纳秒"。但实测是 195× 差距，矛盾极其明显 —— 单凭读代码已无法前进，必须用采样证据。

容器环境受限（无 root、无 `perf`、无 gperftools、无 `FlameGraph.pl`），因此自己写一个最小采样器：

```
test/perf/tools/sampling_profiler.h   -- SIGPROF + setitimer + backtrace(3)
test/perf/tools/profile_decode_packets.cpp  -- 专跑 DecodePackets 的 driver
test/perf/tools/resolve_stacks.py     -- addr2line + c++filt 离线符号化
```

`sampling_profiler.h` 的设计要点：
- `sigaction(SIGPROF, ...)`，`setitimer(ITIMER_PROF)` 设定 997 Hz（质数避免谐波）；
- 信号处理里只做 `backtrace()` + 写无锁 ring buffer（`atomic.fetch_add(write_pos_)`），完全 async-signal-safe；
- 析构时把 ring 以 `"0xADDR;0xADDR;..."` 的"innermost-first"格式 dump 出去；
- 同时 dump 一份 `/proc/self/maps`，让离线 resolver 能把 runtime PC 映射回 (binary, file_offset)。

构建选项关键：`-O2 -g -fno-omit-frame-pointer -rdynamic`。

有一个踩过的坑：`Sample ring_[131072]` ≈ 51 MB，作为类成员放在 `main` 的栈上会直接 segfault —— 改成 `new Sample[131072]` 堆分配即可。

---

## 2. 第一次运行：火焰图上看到"不该有"的符号

用修好栈溢出的 profiler 跑 5 s，抓到 4986 个样本，经 resolver 符号化后 top-inclusive：

```
99.36%  quic::DecodePackets
96.67%  quic::VersionNegotiationPacket::DecodeWithoutCrypto    ← ?!
98.07%  common::SingletonLogger::Info                         ← 不是 Debug，是 Info!
77.94%  common::BaseLogger::Info
62.66%  common::FormatLog
46.45%  __GI___snprintf
32.55%  common::GetFormatTime
21.24%  __tz_convert
15.88%  __tzfile_compute
13.92%  __tzset_parse_tz
12.96%  parse_offset      (libc 时区字符串解析)
```

两点非常可疑：
1. driver 构造的明明是 `InitPacket`，为什么栈顶是 `VersionNegotiationPacket::DecodeWithoutCrypto`？
2. 几乎整个 CPU 花在 `snprintf` + glibc `localtime`/`tzfile` 解析上，这是日志子系统的典型指纹。

`grep LOG_INFO src/quic/packet/` 给出答案：**整个 packet 模块里只有三条 `LOG_INFO`，全在 `version_negotiation_packet.cpp`**：

```cpp
// src/quic/packet/version_negotiation_packet.cpp
common::LOG_INFO("Version Negotiation packet: server supports %u versions", version_count);  // L105
for (size_t i = 0; i < version_count; i++) {
    ...
    common::LOG_INFO("  Version[%zu]: 0x%08x", i, support_version_[i]);                      // L108
}
```

再看 `DecodePackets`：

```cpp
// src/quic/packet/packet_decode.cpp  L57-60
if (version == 0) {
    // Version Negotiation packet (RFC 9000 Section 17.2.1)
    common::LOG_DEBUG("get packet type:version_negotiation (version=0)");
    packet = std::make_shared<VersionNegotiationPacket>(flag.GetFlag());
}
```

——**只要包头的 Version 字段 = 0，就会被当作 Version Negotiation 处理。**

## 3. 真正的根因：driver / benchmark 忘了 `SetVersion`

`LongHeader::LongHeader()` 默认构造把 `version_` 初始化为 0；`InitPacket tx;` 之后如果不显式 `SetVersion()`，`tx.Encode()` 出来的字节流里就带着 version=0。

Benchmark 和 profiler driver 的共同问题：

```cpp
quic::InitPacket tx;
tx.GetHeader()->SetPacketNumberLength(2);  // 只设了 PN 长度
tx.SetPacketNumber(42);
tx.SetPayload(payload);
tx.Encode(encoded);                        // version=0 → wire bytes 被看成 VN 包
```

这种"看上去像 Initial，实际 decode 成 Version Negotiation"的问题在 crypto-enabled 的 benchmark 里不会暴露，因为那里显式走了加密/解密路径（`InitPacket::DecodeWithCrypto` + HP 去保护），错误路径会直接失败。但 `DecodeWithoutCrypto` 不做合法性判断，VN 会乐呵呵地把 256 B payload 当成 **64 条候选版本**处理，每条打两遍 `LOG_INFO` = **128 条 log/包** = **128 次 `localtime`+`snprintf`**。

这也解释了为什么 `--mute-log`（`LOG_SET_LEVEL(kNull)`）下延迟瞬间回到 1321 ns —— log 被静默后，该路径上没有别的热点。

## 4. 修复

在 5 处 `quic::InitPacket tx`-pattern 后都加上：

```cpp
static_cast<quic::LongHeader*>(tx.GetHeader())->SetVersion(quic::GetDefaultVersion());
```

（`IHeader` 是抽象基类不提供 `SetVersion`；`InitPacket::GetHeader()` 声明的返回类型是 `IHeader*`，所以需要显式 `static_cast`。）

影响文件：
- `test/perf/packet_perf_test.cpp`（5 处）
- `test/perf/tools/profile_decode_packets.cpp`（1 处）

同时把 `BM_Packet_SinglePacket_DecodeViaDispatch_NoAlloc` 里的 `PauseTiming()/ResumeTiming()` 去掉 —— google-benchmark 的 Pause/Resume 本身一对就要几百 ns，反而让 "NoAlloc" 版本比 alloc 版本还慢；改为预 `reserve` + 原地 `clear()` 复用 `packets` vector。

## 5. 验证：修复前后对比

```
Benchmark                                          修复前    修复后   加速
------------------------------------------------------------------
BM_Packet_InitPacket_DecodeNoCrypto/256 (手工)     330 ns    293 ns   1.1×
BM_Packet_Coalesced_Decode                       68500 ns   421 ns   163×
BM_Packet_SinglePacket_DecodeViaDispatch         65000 ns   418 ns   155×
BM_Packet_SinglePacket_DecodeViaDispatch_NoAlloc   ---      387 ns   n/a
```

用修好的 driver 重采样，top-inclusive 的画像已经完全健康：

```
15.8%  quic::InitPacket::DecodeWithoutCrypto
 8.0%  operator new               (make_shared<InitPacket> + vector 增长)
 5.6%  StandaloneBufferChunk::Valid
 5.1%  __free
 2.9%  memcpy_avx_unaligned_erms  (SingleBlockBuffer::InnerWrite)
 2.5%  vector::_M_realloc_insert  (packets 默认容量=0)
```

整张 profile 里**完全不再出现** `SingletonLogger::*` / `BaseLogger::*` / `FormatLog` / `tz_convert` / `snprintf`。

单次 `DecodePackets` 从 **62100 ns → 436 ns**（driver 包含 MakeBuf + Write 开销），**143× 加速**。

## 6. 完整 packet_perf_test 当前基线（含所有 6 个新增 P0/P1 benchmark）

| Benchmark                                            | Time   | 吞吐          |
|------------------------------------------------------|-------:|---------------|
| `BM_Packet_InitPacket_EncodeNoCrypto/64`             | 306 ns | 199 MiB/s     |
| `BM_Packet_InitPacket_EncodeNoCrypto/256`            | 310 ns | 786 MiB/s     |
| `BM_Packet_InitPacket_EncodeNoCrypto/1200`           | 376 ns | 2.97 GiB/s    |
| `BM_Packet_InitPacket_EncodeWithCrypto/64`           | 693 ns | 88 MiB/s      |
| `BM_Packet_InitPacket_EncodeWithCrypto/256`          | 730 ns | 334 MiB/s     |
| `BM_Packet_InitPacket_EncodeWithCrypto/1200`         | 1044 ns| 1.07 GiB/s    |
| `BM_Packet_InitPacket_DecodeNoCrypto/64`             | 292 ns | 262 MiB/s     |
| `BM_Packet_InitPacket_DecodeNoCrypto/256`            | 295 ns | 879 MiB/s     |
| `BM_Packet_InitPacket_DecodeNoCrypto/1200`           | 352 ns | 3.22 GiB/s    |
| `BM_Packet_InitPacket_DecodeWithCrypto/64`           | 1293 ns| 70 MiB/s      |
| `BM_Packet_InitPacket_DecodeWithCrypto/256`          | 1337 ns| 205 MiB/s     |
| `BM_Packet_InitPacket_DecodeWithCrypto/1200`         | 1717 ns| 684 MiB/s     |
| `BM_Packet_HandshakePacket_EncodeNoCrypto/*`         | ≈300 ns| ~850 MiB/s    |
| `BM_Packet_Rtt1Packet_EncodeNoCrypto/*`              | ≈280–350 ns | 222 MiB/s – 10.9 GiB/s |
| `BM_Packet_PacketNumber_Encode`                      | 2.14–2.46 ns | 465 M/s |
| `BM_Packet_PacketNumber_DecodeTruncated`             | 2.75 ns| 363 M/s       |
| `BM_Packet_Coalesced_Decode`                         | 421 ns | 616 MiB/s     |
| `BM_Packet_SinglePacket_DecodeViaDispatch`           | 418 ns | 620 MiB/s     |
| `BM_Packet_SinglePacket_DecodeViaDispatch_NoAlloc`   | 387 ns | 670 MiB/s     |

**DecodePackets 的分派开销** = `418 ns - 293 ns` ≈ **125 ns**（含 `HeaderFlag`、version 判断、`make_shared<InitPacket>`、`vector::push_back`）；
**NoAlloc 版本**（共享 packets vector）= `387 ns - 293 ns` ≈ **94 ns**。

## 7. 后续优化建议（未实施）

虽然本次主要问题是 **测试/driver 的 bug**，但火焰图顺便暴露了一些真实的小优化空间：

1. **`DecodePackets` 里 `packets` vector 无 reserve**：如果数据报里有多个 coalesced 包，`_M_realloc_insert` 会再次触发 `operator new`。调用方一般知道上限（多数 ≤ 4），可以在接口上允许传入预分配的 vector。
2. **`make_shared<InitPacket>` 在热路径上**：每个 Initial 包都堆分配一个 `shared_ptr<IPacket>`。如果能让上层用小型对象池（或 std::variant<InitPacket, HandshakePacket, Rtt0Packet, Rtt1Packet, ...>）来承载，至少省掉一次 heap alloc/free。
3. **日志格式化应该在 level 过滤之后进行**。现在 `FormatLog → GetFormatTime → localtime`/`snprintf` 的链条每一次都过一遍时区解析（`__tz_convert → __tzfile_compute → __tzset_parse_tz → parse_offset`），即便是 `LOG_INFO` 在 `level_=kInfo` 时也会走完全程。建议：
   - `BaseLogger::Info(...)` 入口先检查 `level_ & kInfoMask`，不匹配立刻 return，**不要** 先做 `va_start` + `vsnprintf`；
   - `GetFormatTime` 在成功路径上每次都做 `localtime_r`，可以每秒缓存一次（多线程用 atomic<uint64_t> + thread_local cache），因为日志时间戳精度到秒通常够用（已有毫秒部分走单独路径）。
4. **`StandaloneBufferChunk::Valid()` 出现在 top 5.6%**：这个函数在 profile 里被调用得相当频繁，可以考虑 `[[gnu::always_inline]]` 或直接改为内联 bool 返回，避免一次 call+ret。

## 8. 可复现步骤

```bash
# 从源码重新跑一轮基准（全部 6 个 packet perf target）
cd /data/workspace/quicX
cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=Release -DENABLE_PERF_TESTS=ON
cmake --build build-perf --target packet_perf_test profile_decode_packets -j
./build-perf/bin/perf/packet_perf_test --benchmark_min_time=0.3s

# 抓一张 decode 路径的火焰图数据
./build-perf/bin/perf/profile_decode_packets --seconds 5 --hz 997 --out /tmp/decode.raw
python3 test/perf/tools/resolve_stacks.py /tmp/decode.raw --top 25

# 产生的 /tmp/decode.raw.collapsed 可以直接喂给
#   https://github.com/brendangregg/FlameGraph  的 flamegraph.pl
#   生成 SVG 火焰图。
```

---

## 附录：profiler 内部关键设计

- **采样频率 997 Hz**：质数，避免与定时器/OS tick 形成谐波锁相。
- **ring buffer 大小 131072 (`1<<17`)**：对应 5 s * 997 Hz ≈ 4985 样本，留 26× 余量；但该结构体要堆分配（51 MB），不能放栈上。
- **`backtrace(3)`** 是 glibc async-signal-safe 的 —— 它只读 `.eh_frame`/frame pointer，不进 malloc；关键是必须用 `-fno-omit-frame-pointer` 编译，否则 backtrace 只能拿到 1–2 层。
- **非 PIE 二进制的地址映射**：`/proc/self/maps` 显示可执行段 `00400000-00517000 r-xp 00000000`，offset=0，说明 ELF 是 ET_EXEC，runtime PC **等于** addr2line 期待的输入；而 .so（像 `libc.so.6`）是 ET_DYN，需要 `addr - r.start + r.offset` 换算。resolver 里用 `file <binary>` 检测，对非 PIE 直接用原始 PC。这一步如果错了，所有 executable 内的帧都会解析出 "[bin:??]"，故障现象非常隐蔽。

---

# 第二轮：其他 perf target 的火焰图定位与修复（2026-04-30 续）

在修好 packet_perf 的 dispatch 通路后，我们把同一套工具应用到 **QPACK** 与 **BlockedRegistry** 两个疑似热点上，定位并修复了两个真实的算法/实现问题。

## 2.1 疑点来源

跑完其余 5 个 perf target 后的数据筛查发现两处异常量级：

| Benchmark | 实测 | 期望量级 | 可疑点 |
|---|---:|---:|---|
| `BM_Qpack_Encode_LargeHeaders/0` (11 headers) | **9.45 µs** | < 2 µs（~100 ns/header） | encoder 热路径某处有 O(n) 非必要开销 |
| `BM_Qpack_BlockedRegistry_AddAck/1024` | **1042 µs** | < 100 µs | N=16→128→1024 时间从 1.47 → 20 → 1042 µs，**明显超线性**，O(N²) 嫌疑 |

此外还有 `AES-128-GCM Decrypt/4096 > AES-256/4096`、`HKDF-SHA384 4.6× SHA256`、`PathChallenge encode 372 ns` 等二级异常，本轮先专注 QPACK 路径（影响 HTTP/3 每个请求响应）。

## 2.2 两个新的 profiler driver

沿用既有 `SamplingProfiler` + `resolve_stacks.py`，新增：

```
test/perf/tools/profile_qpack_encode.cpp       -- 狂跑 QpackEncoder::Encode()
test/perf/tools/profile_blocked_registry.cpp   -- 狂跑 AckByStreamId / RemoveByStreamId
```

CMake 里用 `add_profiler()` 函数统一管理（`-O2 -g -fno-omit-frame-pointer -rdynamic`）。

## 2.3 BlockedRegistry：O(N²) 实锤

**采样（修复前，N=1024，5 s）**：

```
47.80%  QpackBlockedRegistry::AckByStreamId
46.98%  QpackBlockedRegistry::RemoveByStreamId     <- 合计 94.8% CPU 在这里
 2.27%  Add
 _M_erase (hash-map 删除)                          <-  只占 ~0.3%
```

**根因**（`blocked_registry.cpp` 原实现）：

```cpp
template <typename Map>
typename Map::iterator FindEarliestForStream(Map& pending, uint64_t stream_id) {
    for (auto it = pending.begin(); it != pending.end(); ++it) {
        if ((it->first >> 32) != stream_id) continue;
        // pick the smallest key for this stream_id
    }
}
```

每次 Ack/Remove 都**扫描整张 `pending_` 表**。N=1024 的 fill+drain 需要 1024 次 Add + 1024 次 Ack/Remove，每次 Ack/Remove O(N) → 总 O(N²) ≈ 524k 次迭代，对应实测的 ~1 ms。

**修复**：新增辅助索引 `by_stream_: unordered_map<stream_id, set<key>>`，Find-Earliest 直接取 `begin()`（O(log K)，K = 该 stream 上的 outstanding section 数）：

```cpp
bool QpackBlockedRegistry::AckByStreamId(uint64_t stream_id) {
    auto sit = by_stream_.find(stream_id);
    if (sit == by_stream_.end() || sit->second.empty()) return false;
    uint64_t key = *sit->second.begin();  // 最小值
    sit->second.erase(sit->second.begin());
    if (sit->second.empty()) by_stream_.erase(sit);
    // pending_.erase(key) + 回调
}
```

Add / Remove 同步维护索引。

**效果**：

| N | 修复前 | 修复后 | 变化 |
|---:|---:|---:|---:|
| 16 | 1.47 µs | 2.14 µs | ⚠ 1.5× 慢（索引维护常数 > N=16 的扫描） |
| 128 | 20.1 µs | 24.9 µs | ⚠ 1.2× 慢 |
| **1024** | **1042 µs** | **173 µs** | **6.0× 快** ✅ |

修复后采样显示 `FindEarliestForStream` **完全从 top 消失**，热点退化成 `Add` 路径上的 hashmap `_M_rehash` + `operator new`（~32% CPU，正常的数据结构维护成本）。

N 较小时的常数退化可接受（HTTP/3 高并发场景更关心 N 大时不炸）；如果日后需要两全，可在 Add 里按 size 阈值 lazy-enable 索引。

## 2.4 QPACK encoder：Huffman 是主要瓶颈

**采样（修复前，11 headers、0 cookies，5 s）**：

```
 45.6%  QpackEncoder::Encode
 38.9%  QpackEncoder::EncodeString
 30.5%  HuffmanEncoder::Encode             <- 最大单个被调函数
  2.3%  __free                              (临时 std::vector 释放)
  1.7%  QpackEncodePrefixedInteger
```

每次 `EncodeString` 先调 `ShouldHuffmanEncode`（扫一遍算 bit 数），再调 `Encode`（又扫一遍拿 `std::vector<uint8_t>`），然后 `EncodeString` 把 vector 内容 `buffer->Write` 一次。

**三个具体问题**：
1. `std::vector<uint8_t> output;` 没 `reserve` —— 每次 `emplace_back` 可能触发 realloc（`std::vector` 的 exponential grow 会多次 malloc）
2. `WriteBits` 每次只写 1 字节且用逐 bit mask，没用 64-bit accumulator 一次消化多字节
3. `ShouldHuffmanEncode` + `Encode` 两次遍历（不过这个改起来要动 API 暂不碰）

**修复**（`huffman_encoder.cpp`）：

```cpp
std::vector<uint8_t> HuffmanEncoder::Encode(const std::string& input) {
    // Pre-compute exact output size in one pass, reserve up-front.
    size_t total_bits = 0;
    for (unsigned char c : input) total_bits += huffman_vector_[c].num_bits;
    std::vector<uint8_t> output;
    output.reserve((total_bits + 7) / 8);

    // 64-bit accumulator: all QPACK Huffman codes are <= 30 bits, so
    // (30 + 7 pending) always fits before the drain loop runs.
    uint64_t buf = 0;
    int bits_in_buf = 0;
    for (unsigned char c : input) {
        const HuffmanCode& code = huffman_vector_[c];
        buf = (buf << code.num_bits) | code.code;
        bits_in_buf += code.num_bits;
        while (bits_in_buf >= 8) {
            bits_in_buf -= 8;
            output.push_back((uint8_t)((buf >> bits_in_buf) & 0xFFu));
        }
    }
    if (bits_in_buf > 0) {
        int pad = 8 - bits_in_buf;
        output.push_back((uint8_t)(((buf << pad) | ((1u << pad) - 1u)) & 0xFFu));
    }
    return output;
}
```

保持 API 与字节级输出语义完全不变（QPACK decode 侧的 round-trip benchmark `BM_Qpack_Decode_LargeHeaders` 全部通过 = encoded 字节序列正确）。

**效果**：

| Benchmark | 修复前 | 修复后 | 加速 |
|---|---:|---:|---:|
| `Encode_LargeHeaders/0` (11 headers, 0 cookies) | 9.45 µs | **6.51 µs** | **1.45×** ✅ |
| `Encode_LargeHeaders/8` | 11.1 | 7.67 | 1.45× |
| `Encode_LargeHeaders/32` | 16.3 | 10.9 | 1.50× |
| `Encode_LargeHeaders/64` | 24.4 | 15.0 | **1.63×** |
| `Decode_LargeHeaders/0` (间接受益) | 5.80 | 5.04 | 1.15× |
| `Decode_LargeHeaders/64` | 22.5 | 20.0 | 1.13× |

修复后采样显示 `HuffmanEncoder::Encode` 从 top-inclusive 中**消失**（已被 inline/摊薄进 `EncodeString`），`__free` 相对占比反而**上升**到 6.37% —— 说明固定的逐 bit 成本已降到最低，进一步加速要从 string 管理（`tolower` 临时 string、`std::unordered_map<std::string,std::string>` 的 hash/等比较）或直接写 IBuffer（省掉中间 `std::vector`）去找。

## 2.5 未修但已定位的下一步

按本次采样数据，下一步投入产出比最高的候选：

1. **`HuffmanEncoder::EncodeTo(str, IBuffer&)`**：绕开 `std::vector<uint8_t>` 中间层，可再省 ~5–8% `__free`/`operator new`。
2. **`QpackEncoder::Encode` 的 `tolower` 临时 string**（采样里占 1.86%）：先按原样查静态表，miss 才 tolower。
3. **`AES-128-GCM Decrypt/4096 = 2504 ns` 反常倒挂**（比 AES-256 慢 70%）：疑似每包重建 `EVP_CIPHER_CTX`；需单独写一个 `profile_aead_decrypt` driver 确认。
4. **`HKDF-SHA384` 4.6× 慢于 SHA256**：握手热路径，值得单独分析 BoringSSL `HKDF_expand` 是否走了通用 fallback。

## 2.6 文件变更索引

新增/修改：
- `test/perf/tools/profile_qpack_encode.cpp`（新）
- `test/perf/tools/profile_blocked_registry.cpp`（新）
- `test/perf/CMakeLists.txt`：`add_profiler()` 统一三个 profiler target
- `src/http3/qpack/blocked_registry.h`：新增 `by_stream_` 二级索引
- `src/http3/qpack/blocked_registry.cpp`：`Add/Ack/Remove/AckByStreamId/RemoveByStreamId` 同步维护索引
- `src/http3/qpack/huffman_encoder.cpp`：`Encode` 改 64-bit accumulator + reserve

无 API 变更，无 ABI 破坏，round-trip 测试通过。

