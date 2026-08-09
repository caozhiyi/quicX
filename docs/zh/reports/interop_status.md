# quicX 互操作性测试报告

> 本文档记录 quicX 与主流 QUIC 实现互操作测试的**最新基线结果**，可作为对外引用版本。
> 测试方法、命令与环境依赖见 [`guide/interop_runbook.md`](../guide/interop_runbook.md)。

| 项 | 值 |
|---|---|
| **报告日期** | 2026-08-09 |
| **运行模式** | ns-3 网络仿真器（quic-network-simulator 拓扑） |
| **被测对端数** | 11 个第三方实现 + quicX 自测 |
| **被测场景数** | 14 个 IETF interop 场景 |
| **总用例数** | 322（PASS 216 / FAIL 16 / UNSUPPORTED 94） |
| **有效通过率** | **93.1%**（216 / 232，剔除 UNSUPPORTED） |

> 说明：`quicx → mvfst` 客户端方向已根据最新验证（2026-08-09）更新：handshake / transfer / resumption / zerortt / keyupdate / rebind-addr / rebind-port 现已通过，仅 `http3` 失败。其余单元格仍为 ns-3 基线结果。

---

## TL;DR

- ✅ 在 ns-3 真实链路下，quicX 与 11 个主流实现互通整体表现良好，**有效通过率 93.1%**；
- ✅ `chacha20`、`keyupdate`、`rebind-port`、`rebind-addr`、`multiconnect` 场景达到 100%；
- ✅ `quicx → mvfst` 提升至 8/9（仅 `http3` 失败）；handshake、transfer、resumption、zerortt、keyupdate、rebind-addr、rebind-port 全部通过；
- ⚠️ 仍需 quicX 自身跟进的真实问题：`quicx → aioquic` 的 `retry` 超时（1 起）、`quicx → picoquic | lsquic` 的 `connectionmigration` 超时（2 起）；
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

> 口径说明：runner 实际执行 322 个独立用例（self 计一次）。第 5、6 节按"双向计数"展开（self 在 Server / Client 两行各计一次），故合计比独立口径多 14 条 self 重复。

| 指标 | 数值 |
|------|-------|
| 总用例数 | **322** |
| ✅ Passed | **216** |
| ❌ Failed | **16** |
| `-` Unsupported | **94** |
| **有效通过率（剔除 Unsupported）** | **216 / 232 ≈ 93.1%** |
| 含 Unsupported 通过率 | 216 / 322 ≈ 67.1% |

---

## 4. 连通性矩阵（按场景）

### 4.1 handshake

| quicX 角色 \ 对端 | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| Client (\*↔quicX)   | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

### 4.2 transfer

| quicX 角色 \ 对端 | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| Client (\*↔quicX)   | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

### 4.3 retry

| quicX 角色 \ 对端 | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ | ✅ | ✅ | ✅ |  -  | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| Client (\*↔quicX)   | ✅ | ✅ | ✅ | ✅ |  -  | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ |

### 4.4 resumption

| quicX 角色 \ 对端 | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| Client (\*↔quicX)   | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

### 4.5 zerortt

| quicX 角色 \ 对端 | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |  -  |
| Client (\*↔quicX)   | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |

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
| Client (\*↔quicX)   | ✅ |  -  | ✅ |  -  | ✅ | ✅ | ✅ | ✅ |  -  |  -  | ✅ |  -  |

### 4.11 v2

| quicX 角色 \ 对端 | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ |  -  | ✅ |  -  |  -  | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |  -  |
| Client (\*↔quicX)   | ✅ |  -  | ✅ |  -  |  -  |  -  | ✅ | ✅ | ✅ | ✅ | ✅ |  -  |

### 4.12 rebind-port

| quicX 角色 \ 对端 | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ |  -  |  -  |  -  |  -  | ✅ |  -  | ✅ |  -  |  -  |  -  |  -  |
| Client (\*↔quicX)   | ✅ |  -  |  -  |  -  | ✅ |  -  |  -  | ✅ |  -  |  -  |  -  |  -  |

### 4.13 rebind-addr

| quicX 角色 \ 对端 | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ |  -  |  -  |  -  |  -  | ✅ |  -  |  -  |  -  |  -  |  -  |  -  |
| Client (\*↔quicX)   | ✅ |  -  |  -  |  -  | ✅ |  -  |  -  |  -  |  -  |  -  |  -  |  -  |

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
| handshake             | 23 | 1 | 0 | 23/24 ≈ 95.8% |
| transfer              | 23 | 1 | 0 | 23/24 ≈ 95.8% |
| retry                 | 20 | 2 | 2 | 20/22 ≈ 90.9% |
| resumption            | 22 | 2 | 0 | 22/24 ≈ 91.7% |
| zerortt               | 21 | 2 | 1 | 21/23 ≈ 91.3% |
| http3                 | 20 | 2 | 2 | 20/22 ≈ 90.9% |
| multiconnect          | 23 | 1 | 0 | 23/24 ≈ 95.8% |
| versionnegotiation    | 11 | 1 | 12 | 11/12 ≈ 91.7% |
| chacha20              | 20 | 0 | 4 | 20/20 = 100% |
| keyupdate             | 15 | 0 | 9 | 15/15 = 100% |
| v2                    | 14 | 1 | 9 | 14/15 ≈ 93.3% |
| rebind-port           | 6 | 0 | 18 | 6/6 = 100% |
| rebind-addr           | 4 | 0 | 20 | 4/4 = 100% |
| connectionmigration   | 7 | 3 | 14 | 7/10 = 70.0% |
| **合计（双向计数）**  | **230** | **16** | **90** | **230/246 ≈ 93.5%** |

> 注：双向合计 230 比第 3 节"独立用例 Passed 216"多 14，因为 self 自测在 Server / Client 两行各计一次；剔除 14 条 self 重复后两套口径等价。

---

## 6. 按对端实现汇总（quicX 视角）

> 数据格式：`a / b（含 c 不支持，d 失败）`，表示 14 个场景中 `b` 个有效执行（PASS+FAIL），其中 `a` PASS、`d` FAIL；`c` 个被任一端标为 UNSUPPORTED。

| 对端实现 | quicX 作 Server（X→quicx） | quicX 作 Client（quicx→X） | 总通过 / 总有效 |
|---|:---:|:---:|:---:|
| **self** (quicx↔quicx) | 14/14（全 PASS） | 14/14（全 PASS） | 14 / 14 |
| **quiche**             | 7 / 7（含 7 不支持，全 PASS） | 8 / 8（含 6 不支持，全 PASS） | 15 / 15 |
| **ngtcp2**             | 12 / 12（含 2 不支持，全 PASS） | 11 / 11（含 3 不支持，全 PASS） | 23 / 23 |
| **quic-go**            | 10 / 10（含 4 不支持，全 PASS） | 9 / 9（含 5 不支持，全 PASS） | 19 / 19 |
| **mvfst**              | 4 / 6（含 8 不支持，2 失败） | 8 / 9（含 5 不支持，1 失败） | **12 / 15** |
| **quinn**              | 14 / 14（全 PASS） | 9 / 9（含 5 不支持，全 PASS） | 23 / 23 |
| **aioquic**            | 10 / 10（含 4 不支持，全 PASS） | 9 / 10（含 4 不支持，1 失败）| 19 / 20 |
| **picoquic**           | 13 / 13（含 1 不支持，全 PASS） | 12 / 13（含 1 不支持，1 失败）| 25 / 26 |
| **neqo**               | 10 / 10（含 4 不支持，全 PASS） | 9 / 10（含 4 不支持，1 失败）| 19 / 20 |
| **lsquic**             | 10 / 10（含 4 不支持，全 PASS） | 9 / 10（含 4 不支持，1 失败）| 19 / 20 |
| **msquic**             | 8 / 10（含 4 不支持，2 失败） | 10 / 10（含 4 不支持，全 PASS） | 18 / 20 |
| **s2n-quic**           | 0 / 6（含 8 不支持，**6 失败**）| 9 / 10（含 4 不支持，1 失败）| **9 / 16** |
| **合计** | 111 / 122 | 117 / 123 | **228 / 245 ≈ 93.1%** |

### 几个观察

- **完全互通的 5 个对端**（quicX ↔ X 双向无失败）：`self`、`quiche`、`ngtcp2`、`quic-go`、`quinn`。这五者代表 quicX 已实现稳定的核心兼容面。
- **接近完全互通**（仅 1 起 quicx→X 方向失败）：`aioquic`、`picoquic`、`neqo`、`lsquic`，失败均集中在 `retry` 或 `connectionmigration` 场景。
- **mvfst** 客户端方向已提升至 8/9（仅 `http3` 失败）；服务端方向（resumption、zerortt）仍有失败。
- **s2n-quic 作为 Server** 存在结构性问题（6 起）：镜像启动后立即 exit 1，倾向于第三方镜像 / 版本侧问题。

---

## 7. 失败用例清单（共 16 条）

按"问题归属"分类列出。下方各分组合计 = 1 + 4 + 5 + 6 = **16**。

### A. quicX → mvfst（quicX 自身需关注，1 条）

> 最新验证后，quicX 客户端方向仅 `http3` 仍失败；handshake / transfer / resumption / zerortt 现已通过（此前为收到 190 字节 → `Size mismatch`）。

| # | 场景 | 配对 | 现象 |
|---|---|---|---|
| A1 | http3 | quicx → mvfst | Client exited with code 1（H3 协议层超时） |

### B. quicX → 其他实现（迁移 / 特殊场景，4 条）

| # | 场景 | 配对 | 耗时 | 现象 |
|---|---|---|---|---|
| B1 | retry                | quicx → aioquic   | 40.58 s | Client exited with code 1（超时） |
| B2 | connectionmigration  | quicx → picoquic  | 40.87 s | Client exited with code 1（迁移路径异常） |
| B3 | connectionmigration  | quicx → neqo      | 5.07 s  | Server failed to start（neqo 镜像不响应迁移） |
| B4 | connectionmigration  | quicx → lsquic    | 40.91 s | Client exited with code 1（迁移路径异常） |

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

#### D-b：msquic Client 在 VN / v2 场景不下载文件（1 条）

| # | 场景 | 配对 | 耗时 | 现象 |
|---|---|---|---|---|
| D6 | versionnegotiation | msquic → quicx | 13.07 s | File not downloaded: 1KB.bin（msquic 客户端仅做 VN 探测，不传文件） |

---

## 8. 待跟进问题（按优先级）

### P0 — quicX 自身需修复（共 3 起）

1. **`quicx → mvfst` 的 `http3`**（A1）— H3 协议层超时，quicX 客户端方向 mvfst 唯一剩余失败项。
2. **`quicx → picoquic | lsquic` 的 `connectionmigration` 40 s 超时**（B2、B4）— ns-3 sim 模式暴露的真实迁移问题。
3. **`quicx → aioquic` 的 `retry` 40 s 超时**（B1）— 回归点，需定位 retry token 解码路径。

### P1 — 第三方镜像 / 环境侧（共 13 起）

4. **mvfst Client**（C1–C5，5 起）/ **s2n-quic Client**（D1–D5，5 起）镜像兼容性 — 与官方 interop runner 历史结果趋势一致。
5. **msquic Client** 在 `versionnegotiation` / `v2` 不下载文件（D6）— 属 msquic 镜像固有行为。

---

## 9. 复现命令

```bash
# ns-3 全矩阵（推荐作为对外口径）
cd test/interop
python3 interop_runner.py --matrix --implementations all --use-local-bin \
    --output markdown --output-file logs/latest_matrix_sim.md
```

> 如果 ns-3 sim 因环境受限（少数 macOS / 内核裁剪）拉不起，可临时用 `--no-sim` 桥接模式做快速基线。但 `--no-sim` 下 `rebind-*`、`connectionmigration` 不具备真实链路语义，**不能作为对外发布数据**。使用前请确保 `test/interop/setup_noop.sh` 有执行权限（`chmod +x test/interop/setup_noop.sh`）。
