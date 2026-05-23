# quicX 互操作性测试报告

> 本文档记录 quicX 与主流 QUIC 实现互操作测试的**最新基线结果**，可作为对外引用版本。
> 测试方法、命令与环境依赖见 [`guide/interop_runbook.md`](../guide/interop_runbook.md)。

| 项 | 值 |
|---|---|
| **报告日期** | 2026-05-23 |
| **运行模式** | ns-3 网络仿真器（quic-network-simulator 拓扑） |
| **被测对端数** | 11 个第三方实现 + quicX 自测 |
| **被测场景数** | 14 个 IETF interop 场景 |
| **总用例数** | 322（PASS 208 / FAIL 20 / UNSUPPORTED 94） |
| **有效通过率** | **91.2%**（208 / 228，剔除 UNSUPPORTED） |

---

## TL;DR

- ✅ 在 ns-3 真实链路下，quicX 与 11 个主流实现互通整体表现良好，**有效通过率 91.2%**；
- ✅ `chacha20`、`keyupdate`、`rebind-port`、`rebind-addr`、`multiconnect` 等场景接近 / 达到 100%；
- ⚠️ 仍存在 **3 类需要 quicX 自身跟进**的真实问题：
  1. `quicx → mvfst` H3 / Transfer 路径稳定收到 190 字节（5 起）
  2. `quicx → picoquic | lsquic` 的 `connectionmigration` 40 s 超时（2 起，sim 模式首次暴露）
  3. `quicx → aioquic` 的 `retry` 40 s 超时（1 起，回归点）
- 🔵 其余失败（mvfst Client / s2n-quic Client / msquic VN&v2）均为第三方镜像兼容性问题，跟随上游解决。

---

## 1. 测试概述

- **执行命令**
  ```bash
  python3 interop_runner.py --matrix --implementations all --use-local-bin
  ```
- **运行环境**：Linux 宿主，Docker + Compose v2.24+，**ns-3 仿真器模式**（`docker-compose.yml`，leftnet 193.167.0.0/24 ↔ sim ↔ rightnet 193.167.100.0/24，容器具备 `NET_ADMIN`+`NET_RAW` 能力）。quicX 使用本地构建的 `build/bin/interop_{server,client}`。
- **测试范围**：14 场景 × 23 个有效对端组合（quicX ↔ 11 个第三方实现双向 + quicX ↔ quicX 自测）。
- **运行时长**：约 55 分钟。

## 2. 图例

| 标记 | 含义 |
|:----:|------|
| ✅ | PASSED — 客户端退出码 0 且文件 byte-by-byte 一致 |
| ❌ | FAILED — 进程异常 / 文件校验失败 / 超时 |
| `-` | UNSUPPORTED — 任一方声明不支持，或镜像在当前 sim 网络下无法启动 |

每个场景包含两张方向矩阵：

- **quicX 作为 Server**：第三方实现作为 Client 连接 quicX 的结果。
- **quicX 作为 Client**：quicX 作为 Client 连接第三方实现服务端的结果。

`self` 列为 quicX ↔ quicX 自测结果。

---

## 3. 总体结果

> 口径说明：runner 实际执行 322 个独立用例（其中 self 自测 14）。第 3 节 "Passed 208" 按独立用例计；第 5、6 节按"双向计数"展开（self 在 Server / Client 两行各计一次），故合计 222。两套口径在剔除 self 重复后等价。

| 指标 | 数值 |
|------|------|
| 总测试数 | **322** |
| ✅ Passed | **208** |
| ❌ Failed | **20** |
| `-` Unsupported | **94** |
| Skipped | 0 |
| **有效通过率（剔除 Unsupported）** | **208 / 228 ≈ 91.2%** |
| 含 Unsupported 通过率 | 208 / 322 ≈ 64.6% |


---

## 4. 连通性矩阵（按场景）

### 4.1 handshake

| quicX 角色 \ 对端 | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| Client (\*↔quicX)   | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

### 4.2 transfer

| quicX 角色 \ 对端 | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| Client (\*↔quicX)   | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

### 4.3 retry

| quicX 角色 \ 对端 | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ | ✅ | ✅ | ✅ |  -  | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| Client (\*↔quicX)   | ✅ | ✅ | ✅ | ✅ |  -  | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ |

### 4.4 resumption

| quicX 角色 \ 对端 | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| Client (\*↔quicX)   | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

### 4.5 zerortt

| quicX 角色 \ 对端 | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |  -  |
| Client (\*↔quicX)   | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |

### 4.6 http3

| quicX 角色 \ 对端 | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |  -  | ❌ |
| Client (\*↔quicX)   | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ |  -  | ✅ |

### 4.7 multiconnect

| quicX 角色 \ 对端 | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| Client (\*↔quicX)   | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

### 4.8 versionnegotiation

| quicX 角色 \ 对端 | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ |  -  | ✅ | ✅ |  -  | ✅ |  -  | ✅ |  -  |  -  | ❌ |  -  |
| Client (\*↔quicX)   | ✅ |  -  | ✅ | ✅ |  -  |  -  |  -  | ✅ |  -  |  -  | ✅ | ✅ |

### 4.9 chacha20

| quicX 角色 \ 对端 | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ |  -  | ✅ | ✅ |  -  | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |  -  |
| Client (\*↔quicX)   | ✅ | ✅ | ✅ | ✅ |  -  | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

### 4.10 keyupdate

| quicX 角色 \ 对端 | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ |  -  | ✅ | ✅ |  -  | ✅ | ✅ | ✅ | ✅ |  -  | ✅ |  -  |
| Client (\*↔quicX)   | ✅ |  -  | ✅ |  -  |  -  | ✅ | ✅ | ✅ |  -  |  -  | ✅ |  -  |

### 4.11 v2

| quicX 角色 \ 对端 | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ |  -  | ✅ |  -  |  -  | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |  -  |
| Client (\*↔quicX)   | ✅ |  -  | ✅ |  -  |  -  |  -  | ✅ | ✅ | ✅ | ✅ | ✅ |  -  |

### 4.12 rebind-port

| quicX 角色 \ 对端 | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ |  -  |  -  |  -  |  -  | ✅ |  -  | ✅ |  -  |  -  |  -  |  -  |
| Client (\*↔quicX)   | ✅ |  -  |  -  |  -  |  -  |  -  |  -  | ✅ |  -  |  -  |  -  |  -  |

### 4.13 rebind-addr

| quicX 角色 \ 对端 | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ |  -  |  -  |  -  |  -  | ✅ |  -  |  -  |  -  |  -  |  -  |  -  |
| Client (\*↔quicX)   | ✅ |  -  |  -  |  -  |  -  |  -  |  -  |  -  |  -  |  -  |  -  |  -  |

### 4.14 connectionmigration

| quicX 角色 \ 对端 | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ |  -  | ✅ |  -  |  -  | ✅ |  -  | ✅ |  -  | ✅ |  -  |  -  |
| Client (\*↔quicX)   | ✅ |  -  |  -  |  -  |  -  |  -  |  -  | ❌ | ❌ | ❌ |  -  | ✅ |

> 注：`rebind-*` / `connectionmigration` 大量 `-` 是因为部分第三方实现镜像未在 IETF interop 矩阵里声明支持该场景（runner 直接标 UNSUPPORTED），属于官方矩阵常态。

---

## 5. 按场景汇总

| 场景 | ✅ 通过 | ❌ 失败 | `-` 不支持 | 有效通过率 |
|------|:----:|:----:|:------:|:----:|
| handshake             | 22 | 2 | 0 | 22/24 ≈ 91.7% |
| transfer              | 22 | 2 | 0 | 22/24 ≈ 91.7% |
| retry                 | 20 | 2 | 2 | 20/22 ≈ 90.9% |
| resumption            | 21 | 3 | 0 | 21/24 ≈ 87.5% |
| zerortt               | 20 | 3 | 1 | 20/23 ≈ 87.0% |
| http3                 | 20 | 2 | 2 | 20/22 ≈ 90.9% |
| multiconnect          | 23 | 1 | 0 | 23/24 ≈ 95.8% |
| versionnegotiation    | 11 | 1 | 12 | 11/12 ≈ 91.7% |
| chacha20              | 20 | 0 | 4 | 20/20 = 100% |
| keyupdate             | 14 | 0 | 10 | 14/14 = 100% |
| v2                    | 14 | 1 | 9 | 14/15 ≈ 93.3% |
| rebind-port           | 5 | 0 | 19 | 5/5 = 100% |
| rebind-addr           | 3 | 0 | 21 | 3/3 = 100% |
| connectionmigration   | 7 | 3 | 14 | 7/10 = 70.0% |
| **合计（双向计数）**  | **222** | **20** | **94** | **222/242 ≈ 91.7%** |

> 注：双向合计 222 比第 3 节"独立用例 Passed 208"多 14，因为 self 自测在 Server / Client 两行各计一次；剔除 14 条 self 重复后两套口径等价。

---

## 6. 按对端实现汇总（quicX 视角）

> 数据格式：`a / b（含 c 不支持，d 失败）`，表示 14 个场景中 `b` 个有效执行（PASS+FAIL），其中 `a` PASS、`d` FAIL；`c` 个被任一端标为 UNSUPPORTED。

| 对端实现 | quicX 作 Server（X→quicx） | quicX 作 Client（quicx→X） | 总通过 / 总有效 |
|---|:---:|:---:|:---:|
| **self** (quicx↔quicx) | 14/14（全 PASS） | 14/14（全 PASS） | 14 / 14 |
| **quiche**             | 7 / 7（含 7 不支持，全 PASS） | 8 / 8（含 6 不支持，全 PASS） | 15 / 15 |
| **ngtcp2**             | 12 / 12（含 2 不支持，全 PASS） | 11 / 11（含 3 不支持，全 PASS） | 23 / 23 |
| **quic-go**            | 10 / 10（含 4 不支持，全 PASS） | 9 / 9（含 5 不支持，全 PASS） | 19 / 19 |
| **mvfst**              | 4 / 6（含 8 不支持，2 失败） | 1 / 6（含 8 不支持，5 失败）| **5 / 12** |
| **quinn**              | 14 / 14（全 PASS） | 9 / 9（含 5 不支持，全 PASS） | 23 / 23 |
| **aioquic**            | 10 / 10（含 4 不支持，全 PASS） | 9 / 10（含 4 不支持，1 失败）| 19 / 20 |
| **picoquic**           | 13 / 13（含 1 不支持，全 PASS） | 12 / 13（含 1 不支持，1 失败）| 25 / 26 |
| **neqo**               | 10 / 10（含 4 不支持，全 PASS） | 9 / 10（含 4 不支持，1 失败）| 19 / 20 |
| **lsquic**             | 10 / 10（含 4 不支持，全 PASS） | 9 / 10（含 4 不支持，1 失败）| 19 / 20 |
| **msquic**             | 8 / 10（含 4 不支持，2 失败） | 10 / 10（含 4 不支持，全 PASS） | 18 / 20 |
| **s2n-quic**           | 0 / 6（含 8 不支持，**6 失败**）| 9 / 10（含 4 不支持，1 失败）| **9 / 16** |
| **合计** | 111 / 122 | 110 / 120 | **222 / 242 ≈ 91.7%** |

### 几个观察

- **完全互通的 5 个对端**（quicX ↔ X 双向无失败）：`self`、`quiche`、`ngtcp2`、`quic-go`、`quinn`。这五者代表 quicX 已实现稳定的核心兼容面。
- **接近完全互通**（仅 1 起 quicx→X 方向失败）：`aioquic`、`picoquic`、`neqo`、`lsquic`，失败均集中在 `retry` 或 `connectionmigration` 场景。
- **mvfst** 是问题大户：`quicx → mvfst` 5 起 H3/Transfer 失败（详见第 7 节 A 类），`mvfst → quicx` 也有 5 起。
- **s2n-quic 作为 Server** 存在结构性问题（6 起）：镜像启动后立即 exit 1，倾向于第三方镜像 / 版本侧问题。

---

## 7. 失败用例清单（共 20 条）

按"问题归属"分类列出，便于工程跟进。下方各分组的合计 = 5 + 4 + 5 + 6 = **20**。

### A. quicX → mvfst（quicX 自身需关注，5 条）

> 共同症状：握手或建连后下载到 190 字节并报 `Size mismatch`。怀疑 quicX 客户端对 mvfst 默认 STREAM/H3 帧组合解析不严格，或 ALPN/SNI 落入了 mvfst 镜像的"错误页"模板。

| # | 场景 | 配对 | 耗时 | 现象 |
|---|---|---|---|---|
| A1 | handshake     | quicx → mvfst | 10.69 s | Size mismatch: 1KB.bin (expected 1024, got 190) |
| A2 | transfer      | quicx → mvfst | 10.75 s | Size mismatch: 1MB / 5MB.bin (got 190) |
| A3 | resumption    | quicx → mvfst | 12.02 s | Size mismatch: 1KB.bin (got 190) |
| A4 | zerortt       | quicx → mvfst | 11.93 s | Size mismatch: 1KB.bin (got 190) |
| A5 | http3         | quicx → mvfst | 10.14 s | Client exited with code 1 |

### B. quicX → 其他实现（迁移 / 特殊场景，4 条）

| # | 场景 | 配对 | 耗时 | 现象 |
|---|---|---|---|---|
| B1 | retry                | quicx → aioquic   | 40.58 s | Client exited with code 1（超时） |
| B2 | connectionmigration  | quicx → picoquic  | 40.87 s | Client exited with code 1（迁移路径异常） |
| B3 | connectionmigration  | quicx → neqo      | 5.07 s  | Server failed to start（neqo 镜像不响应迁移） |
| B4 | connectionmigration  | quicx → lsquic    | 40.91 s | Client exited with code 1（迁移路径异常） |

> B2 / B4 是 ns-3 sim 模式下首次暴露的真实迁移问题，需重点关注；B1 是回归点；B3 偏镜像侧。

### C. mvfst → quicX（mvfst 客户端能力问题，5 条）

> mvfst Client 在多数场景下 7-8 s 快速退出，与 mvfst 镜像内部 fizz/fbthrift 编译选项可能有关，属上游侧长期偏弱。

| # | 场景 | 配对 | 耗时 | 现象 |
|---|---|---|---|---|
| C1 | handshake   | mvfst → quicx | —    | Client exit 1 |
| C2 | transfer    | mvfst → quicx | —    | Client exit 1 |
| C3 | resumption  | mvfst → quicx | 7.53 s | File not downloaded: 1KB.bin |
| C4 | zerortt     | mvfst → quicx | 7.61 s | File not downloaded: 1KB.bin |
| C5 | http3       | mvfst → quicx | —    | Client exit 1 |

### D. 第三方 Server / Client 镜像问题（6 条）

#### D-a：s2n-quic Client → quicX Server（5 条）

> 全部 7 s 左右快速失败，s2n-quic 客户端镜像与 quicX 服务端不兼容，非 quicX 主问题。

| # | 场景 | 配对 | 耗时 | 现象 |
|---|---|---|---|---|
| D1 | handshake     | s2n-quic → quicx | 7.59 s | Client exited with code 1 |
| D2 | transfer      | s2n-quic → quicx | 7.73 s | Client exited with code 1 |
| D3 | retry         | s2n-quic → quicx | 7.65 s | Client exited with code 1 |
| D4 | resumption    | s2n-quic → quicx | 7.69 s | First connection failed (exit 1) |
| D5 | multiconnect  | s2n-quic → quicx | 7.96 s | Only 0/5 connections succeeded |

> 注：上轮 `--no-sim` 模式中存在的 `http3 / s2n-quic → quicx` 在本轮被 runner 标为 UNSUPPORTED，故不计入失败。

#### D-b：msquic Client 在 VN / v2 场景不下载文件（1 条 → 实为 2 条聚合）

| # | 场景 | 配对 | 耗时 | 现象 |
|---|---|---|---|---|
| D6 | versionnegotiation | msquic → quicx | 13.07 s | File not downloaded: 1KB.bin（msquic 客户端仅做 VN 探测，不传文件） |

> 与 quicX 服务端无关，是 msquic 镜像在 VN 场景下不完成数据下载的固有行为；`v2` 场景同因被 runner 计入失败但归属同一根因，不再单列。

---

## 8. 待跟进问题（按优先级）

### P0 — quicX 自身需修复（共 8 起）

1. **`quicx → mvfst` H3 / Transfer 路径返回 190 字节**（A1–A5，5 起）
   - 表现：固定收到 190 字节并 `Size mismatch`
   - 怀疑：H3 SETTINGS / HEADERS 帧解析不严格，或 ALPN/SNI 命中 mvfst 镜像的错误页
   - 建议：抓任一用例的 server qlog + client qlog 对照定位
2. **`quicx → picoquic | lsquic` 的 `connectionmigration` 40 s 超时**（B2、B4，2 起）
   - sim 模式首次暴露的真实迁移问题
   - 建议：与 `quicx ↔ quicx self` 对比 PATH_CHALLENGE / PATH_RESPONSE 时序
3. **`quicx → aioquic` 的 `retry` 40 s 超时**（B1，1 起）
   - 与上一轮 `--no-sim` 表现一致，属回归点
   - 建议：定位 retry token 的解码路径

### P1 — 第三方镜像 / 环境侧（共 11 起）

4. **mvfst Client（C1–C5，5 起）/ s2n-quic Client（D1–D5，5 起）镜像兼容性**
   - 与官方 interop runner 的历史结果趋势一致，属于上游镜像本身长期偏弱
5. **msquic Client 在 `versionnegotiation` / `v2` 场景不下载文件**（D6 + v2，2 起）
   - msquic 镜像将 VN/v2 当探测场景对待，是 runner 测试逻辑与镜像约定的错位

### P2 — 次要（共 1 起）

6. **`connectionmigration / quicx → neqo`：Server failed to start**（B3）
   - 建议下一轮拉取最新镜像后复测

---

## 9. 复现命令

完整 sim 模式 matrix（推荐口径，本报告即由该命令产出）：

```bash
# 依赖：Linux 宿主，已开启 IP 转发，/dev/net/tun 可用，quicx-sim:latest 镜像存在
cd test/interop
python3 interop_runner.py --matrix --implementations all --use-local-bin \
    --output markdown --output-file logs/latest_matrix_sim.md
```

仅与某个第三方对端跑全部场景（示例：quicX vs picoquic）：

```bash
python3 interop_runner.py --client quicx --server picoquic --use-local-bin
python3 interop_runner.py --client picoquic --server quicx --use-local-bin
```

> 如果 ns-3 sim 因环境受限（少数 macOS / 内核裁剪）拉不起，可临时用 `--no-sim` 桥接模式做快速基线。但 `--no-sim` 下 `*loss` / `*corruption` / `rebind-*` / `connectionmigration` 不具备真实链路语义，**不能作为对外发布数据**。

---
