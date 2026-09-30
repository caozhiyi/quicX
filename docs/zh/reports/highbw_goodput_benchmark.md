# 跨实现高带宽吞吐对比（Self-Pair Goodput Benchmark）

本文档记录一次基于 `quic-interop-runner` 官方框架、但把带宽档位从官方标准的 10Mbps 大幅调高后的自测吞吐对比。

目的是在更贴近真实高速网络的场景下，横向对比 quicX 与 9 个主流 QUIC 实现的吞吐表现，并验证 quicX 自身的拥塞控制/流控配置是否已经调到最优。

与 [`reports/interop_status.md`](interop_status.md) 中官方 `goodput`（10Mbps）场景的结果不冲突，是对其在更高带宽区间的补充实验，**非官方矩阵的一部分，非契约**。

---

## 1. 背景与动机

官方 `quic-interop-runner` 的 `goodput` 测算场景固定在 `simple-p2p --delay=15ms --bandwidth=10Mbps --queue=25`，这个带宽档位偏低，无法反映 quicX 在文件传输/流媒体等高带宽场景下的真实吞吐能力。为此写了一个复用官方框架能力的自测脚本 `run_highbw.py`（位于 `quic-interop-runner/` 仓库），只做两处改动：

1. 把 `MeasurementGoodput` 的 ns3 链路带宽参数从 10Mbps 调高至任意值；
2. 把结果校验从官方默认的 `pyshark`（逐包生成 Python 对象，大文件耗时数分钟）换成原生 `tshark -T fields` 一次性字段抽取（秒级），计时方式与官方一致——取 sim 抓包中第一个/最后一个 1-RTT 数据包的时间戳差。

其余容器编排、ns3 拓扑、`docker-compose.yml`、证书生成全部沿用官方 `quic-interop-runner` 逻辑，**不是重新造轮子**。

---

## 2. 关键发现：仿真器本身存在转发能力天花板

在直接冲高带宽之前，先做了一轮探测，发现了一个比"哪个实现更快"更重要的事实。

### 2.1 现象：1Gbps 档效率不到 30%

| 带宽档位 | quicx goodput | 理论占比 |
|---|---|---|
| 100Mbps | 92.9 Mbps | 92.9% |
| 1Gbps | 259.5 Mbps | 25.9% |

10 个实现在 1Gbps 档全部集中在 180-260Mbps 区间，效率随带宽升高骤降，而不是维持某个固定比例。

### 2.2 根因排查

- **没有丢包重传**：1Gbps 与 100Mbps 传输同一个 100MB 文件，QUIC 包总数几乎相同（80059 vs 82757），说明高带宽档没有触发额外重传，不是拥塞控制在退让。
- **真实 RTT 远高于配置值**：`--delay 1ms` 只是链路段单程延迟，从 pcap 实测到的完整往返时间（Initial → Initial+ACK）约 **8.3ms**，是配置值的 8 倍多——ns3 `RealtimeSimulatorImpl` + `EmuFdNetDevice` 的调度/排队开销叠加在链路延迟之上。
- **反算有效窗口高度聚集**：`窗口 = 吞吐量 × RTT / 8`，quicx ≈263KB、msquic ≈265KB、quic-go ≈244KB、lsquic ≈186KB——量级与各实现默认（未调优）流控窗口吻合，且 263KB ÷ 8.3ms ≈ 253Mbps，正好对上 1Gbps 档的实测天花板。
- **仿真器转发能力探测**：10Gbps 档直接返回 `unsupported`（3.6 秒内退出）；300Mbps 与 500Mbps 档的实测吞吐几乎相同（257 vs 262Mbps），说明真正的瓶颈在 **ns3 单进程实时转发管道**（必须把两个 veth 网卡上的每个真实包实时喂给离散事件仿真器，同时两侧还挂着全量 `dumpcap` 抓包），而不在任何一个 QUIC 实现的协议栈里。
- **交叉验证**：10 个完全不同语言/架构的实现（Go/Rust/C/C++）在 1Gbps 档全部收敛到 180-260Mbps 这个窄区间——如果瓶颈在各自的流控窗口，默认值不可能碰巧全部卡在同一区间；但如果瓶颈是共享的仿真器转发能力，所有实现撞上同一个天花板才合理。

**结论**：ns3 仿真链路的真实转发天花板在 **~260Mbps 附近**，明显低于配置的链路带宽上限。在天花板以上调各实现的流控/拥塞参数不会产生任何可观测的改善——因为限流阀根本不在协议栈里。

### 2.3 安全区选定

| 带宽档位 | quicx goodput | 效率 |
|---|---|---|
| 100Mbps | 92.9 Mbps | 92.9% |
| 150Mbps | — | 90%+（探测通过） |
| 200Mbps | 182.3 Mbps | 91.2% |
| 300Mbps | ~257 Mbps | 85.7%（已顶到天花板） |
| 500Mbps | ~262 Mbps | 52.4%（已顶到天花板） |

选定 **200Mbps** 作为后续对比与调参的安全区——距天花板尚有 25%+ 余量，各实现的流控/拥塞算法差异能真实反映在数字上，而不会被仿真器本身的能力上限掩盖。

---

## 3. 200Mbps 安全区：10 实现 self-pair 基线

- 链路：`200Mbps`，delay `1ms`（实测 RTT ≈8ms），queue `1000`
- 传输：`100MB`，每个实现重复 3 次
- 配对方式：self-pair（同实现对打，client 与 server 均为同一实现），避免混入"谁更能兼容谁"的干扰变量
- 计时方式：sim 侧抓包，首个与末个 1-RTT 数据包时间戳差（与官方 `MeasurementGoodput` 方法一致）

| 实现 | Goodput (Mbps) | stdev | 效率（/200Mbps） |
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

**观察**：

- quicx 以 182.3Mbps 位居第三，与 picoquic / lsquic 处于同一梯队，stdev 仅 0.4——是全部 10 个实现里波动最小的，说明 quicx 在这个安全区带宽下吞吐**非常稳定可复现**。
- quiche 的 stdev 高达 52.4（3 次重复里有明显离群值），ngtcp2 也有 24.4 的较高波动，猜测与各自默认拥塞控制算法的收敛特性或某次重复的宿主机调度抖动有关，本次未继续深挖（不在 quicx 范围内）。
- xquic / aioquic 效率显著偏低（<35%），推测是各自默认流控窗口在这个 BDP 下明显偏小；因为二者是黑盒 QNS 镜像（见下一节），未做进一步调优验证。

---

## 4. quicX 参数调优：拥塞控制算法扫描

### 4.1 各实现参数可调性调研

在着手"调参再测"之前，先排查了 10 个 QNS 官方镜像各自的 `run_endpoint.sh` 暴露了哪些调优接口：

| 实现 | 调优接口 | 说明 |
|---|---|---|
| **quicx** | 源码可改 + 新增 `QUICX_CC` 环境变量 | 自己的项目，直接加钩子 |
| picoquic / msquic / lsquic | `SERVER_PARAMS` / `CLIENT_PARAMS` 环境变量透传任意 CLI 参数 | 官方镜像自带的"后门"，本次未启用 |
| ngtcp2 | 无需额外配置 | `run_endpoint.sh` 已经硬编码 `--cc bbr`，相当于自带调优 |
| quic-go / quiche / aioquic / xquic / quinn | 无 | `run_endpoint.sh` 完全没有暴露调优开关，属于黑盒镜像 |

结论：quicx 是唯一可以低成本、无风险地做参数扫描的对象；对其余黑盒镜像做侵入式改动（fork 镜像逆向调参）成本和风险不成比例，本次不做。

### 4.2 quicX 流控窗口现状

`include/quicx/quic/type.h` 中 `QuicTransportParams` 的默认值已经相当宽裕，专为高吞吐场景设计：

| 参数 | 默认值 |
|---|---|
| `initial_max_data_`（连接级） | 64 MB |
| `initial_max_stream_data_bidi_local_/_remote_` | 16 MB |
| `initial_max_stream_data_uni_` | 16 MB |

200Mbps × 8ms RTT 的 BDP 仅 ~200KB，远低于 16MB 的流级窗口——流控窗口在这个场景下**不是瓶颈**，因此本次调优聚焦在拥塞控制算法选择上。

### 4.3 新增 `QUICX_CC` 环境变量

为了不重新编译镜像即可切换算法，在 `test/interop/interop_server.cpp` 与 `interop_client.cpp` 中新增：

```cpp
// Congestion control algorithm override, for benchmarking without
// rebuilding: "reno" | "cubic" | "bbrv1" | "bbrv2" | "bbrv3".
const char* cc = std::getenv("QUICX_CC");
if (cc) {
    config.config_.congestion_control_ = cc;
}
```

并在 `quic-interop-runner/docker-compose.yml` 的 server/client 环境变量块中透传 `QUICX_CC=$QUICX_CC`，`run_highbw.py` 增加 `--cc` 命令行参数（通过 `TestCase.additional_envs()` 扩展点注入，对不识别该变量的其他实现镜像无副作用）。

### 4.4 扫描结果

200Mbps 安全区，5 种算法各重复 3 次：

| 拥塞控制算法 | Goodput (Mbps) | stdev |
|---|---|---|
| reno | 183.2 | 0.8 |
| **cubic（当前默认）** | 182.1 | 0.8 |
| bbrv1 | 183.1 | 0.1 |
| bbrv2 | 182.2 | 1.1 |
| bbrv3 | **119.5** | 6.1 |

**发现 1：reno / cubic / bbrv1 / bbrv2 四种算法统计意义上完全等价**（182-183Mbps，差异在误差范围内）。这符合预期——200Mbps × 8ms RTT 属于低 BDP 场景，慢启动之后很快就能撞到 200Mbps 的链路上限，算法之间在"如何逼近带宽上限"这件事上几乎没有发挥空间。也说明 quicx 当前的默认配置（cubic + 慷慨的流控窗口）在这个场景下**已经是最优**，调整算法或加大窗口都不会带来提升。

**发现 2：bbrv3 明显退步，只有 119.5Mbps（-35% vs 其余四种算法）**。这是一个值得记录、后续单独排查的真实现象——猜测与 `cc_config.h` 中 BBRv3 的 ProbeRTT 周期（10s 触发 / 200ms 降窗持续）在这种约 1 分钟的短时传输里占比过高有关，也可能是启动阶段（Startup）或带宽探测阶段的 pacing gain 对这个特定 BDP 区间偏保守。**这是一个已知但尚未修复的问题**，留给后续版本跟进（不在本次测算范围内展开）。

---

## 5. 绕过仿真器：原生 Loopback 测试（无 ns3、无 docker）

第 2 节已经证明 ns3 仿真链路本身有 ~260Mbps 的转发天花板。要测出 quicX **不受仿真器限制的真实吞吐上限**，需要彻底绕开 `quic-interop-runner` 的容器编排，有两个可选层次：

| 方式 | 绕开了什么 | 还剩下什么开销 | 适用场景 |
|---|---|---|---|
| `test/interop/docker-compose-direct.yml`（直连编排；该文件已随自建 interop 环境移除，可从 git 历史找回） | ns3 仿真与 tc/netem 限速 | docker bridge / veth 转发开销 | 需要保留容器隔离，或与其他仍以镜像形式提供的实现对比 |
| **原生 host 进程 + loopback**（本节） | ns3 **和** docker 网络 | 仅剩 quicX 自身协议栈处理开销 | 测"协议栈本身能跑多快"的绝对上限 |

本节采用后者：直接在宿主机上跑 `interop_server` / `interop_client` 两个进程，通过 `127.0.0.1` 通信，不经过任何容器或网络仿真。

### 5.1 方法

1. 编译原生二进制（无需 docker）：
   ```bash
   cmake --build build --target interop_server interop_client -j$(nproc)
   ```
2. 用新增的 `test/interop/bench_loopback.sh` 脚本起服务端、跑客户端 N 次并统计均值/stdev：
   ```bash
   ./test/interop/bench_loopback.sh --size-mb 500 --reps 5 [--cc cubic]
   ```
3. **计时方式**：直接量客户端进程的墙钟耗时（进程启动到退出）。loopback 场景下握手 RTT 不到 1ms，相对几秒钟的传输耗时可忽略，不需要再靠 `tcpdump`/`tshark` 抓包对时间戳（且 loopback 抓包在容器化 CI 环境里往往缺 `CAP_NET_RAW` 权限）。

### 5.2 结果：quicX 单连接吞吐存在一个比 ns3 天花板更低的独立上限

| 文件大小 | 重复次数 | Goodput (Mbps) | stdev |
|---|---|---|---|
| 100MB | 5 | 472.2 | 8.4 |
| 500MB | 5 | 633.9 | 18.2 |
| 1000MB | 3 | 638.7 | 11.6 |

- 100MB 档偏低是因为连接建立/慢启动的固定开销在总时长里占比还不小；500MB 与 1000MB 已经收敛到同一个稳定值 **~635Mbps**，说明这才是真实稳态上限，不是还在爬坡。
- **~635Mbps 明显低于 loopback 网卡正常能跑到的量级（通常数十 Gbps）**，说明瓶颈不在内核网络栈或 docker，而在 quicX 自己的单连接处理路径上（大概率是 per-packet 的加解密 + 用户态/内核态拷贝 + 系统调用开销，单线程处理单条连接）。这与 [`design/udp_io.md`](../design/udp_io.md) 中讨论的 GSO/sendmmsg/recvmmsg 批量收发路径直接相关，是后续性能优化的一个具体、可复现的目标。

### 5.3 拥塞控制算法扫描：loopback 下 5 种算法完全等价，且 bbrv3 异常不复现

500MB，5 种算法各 3 次：

| 算法 | Goodput (Mbps) | stdev |
|---|---|---|
| reno | 625.7 | 5.3 |
| cubic | 624.1 | 18.4 |
| bbrv1 | 619.4 | 11.9 |
| bbrv2 | 631.1 | 15.8 |
| bbrv3 | 613.0 | 11.9 |

- 5 种算法的结果彼此落在误差范围内，**与 §4.4 的结论一致且进一步验证**：quicX 的吞吐上限由单连接处理开销决定，跟拥塞控制算法完全无关（loopback 下 RTT≈0，BDP 近乎 0，任何算法都会立刻把窗口打满）。
- **关键交叉验证**：§4.4 里 bbrv3 在 200Mbps/8ms RTT 场景下的 -35% 异常，在这里（RTT≈0）**没有复现**（613 vs 其余四种的 619-631，差异在噪声范围内）。这支持了之前的猜测方向——bbrv3 的退步跟 **RTT/ProbeRTT 周期**有关，而不是一个跟带宽或吞吐路径本身相关的通用 bug，为后续排查缩小了范围。

---

## 6. 结论与建议

1. **官方 `goodput`（10Mbps）场景之外，quicX 在 200Mbps 安全区的吞吐效率为 91.2%**，与 picoquic / lsquic / msquic 处于同一梯队，且是波动最小（stdev 最低）的实现。
2. **1Gbps 及以上档位的低效率是 `quic-interop-runner` 自带 ns3 仿真器的转发能力天花板（~260Mbps）导致的**，不是任何 QUIC 实现的缺陷。
3. **绕开 ns3 与 docker 后，quicX 原生 loopback 的单连接稳态吞吐上限约为 635Mbps**，比仿真器天花板还高，但远低于 loopback 网卡的物理量级——说明还有一层独立于仿真环境的、quicX 自身协议栈处理开销带来的上限，是后续性能优化（批量收发 I/O、per-packet 加解密开销）的具体线索。
4. **quicX 当前默认配置（cubic + 64MB/16MB 流控窗口）在有限 BDP 场景下已经是最优解**；`QUICX_CC` 环境变量与 `bench_loopback.sh` 作为长期保留的调优/调试工具，方便后续对新场景重跑扫描。
5. **bbrv3 在有 RTT（尤其伴随 ProbeRTT 周期）的场景下有明显吞吐回退，但在 RTT≈0 的 loopback 场景下不复现**——已将排查范围收窄到 RTT/ProbeRTT 相关逻辑，留待后续版本修复验证。

---

## 7. 复现方法

```bash
cd quic-interop-runner

# 200Mbps 安全区，10 实现 self-pair 基线
python3 run_highbw.py \
  --impls quicx,quic-go,ngtcp2,quiche,msquic,lsquic,picoquic,xquic,quinn,aioquic \
  --bandwidth 200Mbps --delay 1ms --queue 1000 \
  --filesize-mb 100 --repetitions 3 \
  --out-dir highbw_results

# quicX 拥塞控制算法扫描（--cc 仅对 quicx 生效，其他实现忽略该环境变量）
for cc in reno cubic bbrv1 bbrv2 bbrv3; do
  python3 run_highbw.py --impls quicx --cc $cc \
    --bandwidth 200Mbps --delay 1ms --queue 1000 \
    --filesize-mb 100 --repetitions 3 --out-dir /tmp/cc_sweep
  docker rm -f sim server client
done
```

> 注：固定容器名（`sim`/`server`/`client`）意味着上一轮残留容器会导致下一轮静默失败（空结果而非报错），`run_highbw.py` 已在 `run_self_pair()` 前加自动清理；手动多轮测试仍建议在每轮之间执行 `docker rm -f sim server client`。

```bash
cd quicX

# 编译原生二进制
cmake --build build --target interop_server interop_client -j$(nproc)

# 原生 loopback 基线（无 ns3、无 docker）
./test/interop/bench_loopback.sh --size-mb 500 --reps 5

# 原生 loopback 拥塞控制算法扫描
for cc in reno cubic bbrv1 bbrv2 bbrv3; do
  ./test/interop/bench_loopback.sh --size-mb 500 --reps 3 --cc $cc
done
```
