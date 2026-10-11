# quicX 互操作性测试报告

本文档记录 quicX 与主流 QUIC 实现互操作测试的**最新基线结果**，可作为对外引用版本。

测试方法、命令与环境依赖见 [`guide/interop_runbook.md`](../guide/interop_runbook.md)。

| 项 | 值 |
|---|---|
| **运行模式** | 上游 quic-interop-runner 全场景矩阵（quic-network-simulator / ns-3 拓扑） |
| **被测对端数** | 17 个第三方实现（14 双向 + nginx / haproxy 仅 Server + chrome 仅 Client） |
| **被测场景数** | 24 个（22 个协议功能场景 + 2 个性能测算场景） |
| **总用例数** | 768（PASS 625 / FAIL 48 / UNSUPPORTED 95），含 quicX ↔ quicX 自测项 |
| **综合有效通过率** | **92.87%**（625 / 673，剔除 UNSUPPORTED） |
| **协议功能合规通过率** | **93.62%**（572 / 611；Client 模式 294/312 = 94.23%，Server 模式 278/299 = 92.98%） |
| **性能测算通过率** | **85.48%**（53 / 62） |

---

## 总体情况

- ✅ 24 场景 × 17 对端全矩阵 + quicX ↔ quicX 自测（768 用例，2026-10-10 跑测）下，quicX **综合有效通过率 92.87%**，较上一轮（90.60%）提升 2.27pp；
- ✅ **12 个场景 100% 通过**：`handshake`、`transfer`、`longrtt`、`chacha20`、`retry`、`http3`、`blackhole`、`ecn`、`transferloss`、`transfercorruption`、`ipv6`，以及 `goodput`（30/30 满分，平均 9.17 Mbps，最高带宽利用率 95.5%）；
- ✅ **Server 模式显著恢复**（86.28% → 92.98%）：`amplificationlimit`（6/14 → 13/14）、`rebind-addr`（10/14 → 14/14）、`rebind-port`（10/14 → 13/14）、`handshakeloss`（10/13 → 12/13）大面积恢复通过；Client 模式 `amplificationlimit`（14/16 → 15/16）、`handshakeloss`（14/15 → 15/15）同步改善；
- ⚠️ 本轮主要回归：`crosstraffic` 27/30 → 21/30（Client 模式 14/16 → 8/16，`ngtcp2` / `go-x-net` / `quic-go` / `quiche` / `aioquic` / `nginx` 未达阈值）；
- ⚠️ 当前薄弱项：`connectionmigration`（7/19，36.8%）、`rebind-addr`（22/30，73.3%）、`rebind-port`（23/30，76.7%）、Server 模式 `keyupdate`（8/10）与 `zerortt`（10/12）。

---

## 1. 测试概述

- **数据来源**：上游 [quic-interop-runner](https://github.com/quic-interop/quic-interop-runner) 全场景矩阵一轮完整运行（结果文件 `quicx_as_client_full.json` / `quicx_as_server_full.json`，日志目录 `logs_full_{client,server}_20261010_142617`）。
- **运行环境**：Linux 宿主，Docker + ns-3 网络仿真（leftnet ↔ sim ↔ rightnet），quicX 使用本地构建的 `interop_{server,client}`。
- **测试范围**：24 场景 ×（16 个 Server 对端 + 15 个 Client 对端 + quicX ↔ quicX 自测）= **768 个组合**；自测项 Server 方向 21/21 全部通过。
- **与前基线差异**：上一轮为 24 场景 × 17 对端口径（744 用例，有效通过率 90.60%）。本轮综合通过率提升 2.27pp，主要来自 Server 模式 `amplificationlimit`、`rebind-addr`、`rebind-port`、`handshakeloss` 等恢复，Client 模式 `amplificationlimit`、`handshakeloss` 改善，`connectionmigration`（6/19 → 7/19）小幅提升；回归集中在 Client 模式 `crosstraffic`（14/16 → 8/16）。上游矩阵不包含 `multiconnect` / `versionnegotiation`。

## 2. 图例

| 标记 | 含义 |
|:----:|------|
| ✅ | SUCCEEDED — 测试通过 / 速率达标 |
| ❌ | FAILED — 测试失败 / 吞吐未达阈值 |
| ➖ | UNSUPPORTED — 对端或场景不支持 |
| — | N/A — 未进行该组合测试（对端未部署该角色） |

## 3. 总体结果

| 指标 | 数值 |
|------|-------|
| 总用例数 | **768** |
| ✅ 成功 | **625** |
| ❌ 失败 | **48** |
| ➖ 不支持 | **95** |
| **综合有效通过率（剔除 Unsupported）** | **625 / 673 ≈ 92.87%** |
| 协议功能合规通过率 | 572 / 611 ≈ 93.62%（Client 模式 294/312 = 94.23%；Server 模式 278/299 = 92.98%） |
| 性能测算通过率 | 53 / 62 ≈ 85.48%（Goodput 30/30；CrossTraffic 21/30） |
| 含 Unsupported 通过率 | 625 / 768 ≈ 81.4% |

## 4. 按场景维度通过率

| 测试场景 | 场景简称 | Client 模式通过率 | Server 模式通过率 | 综合有效通过率 | 描述 |
| :--- | :---: | :---: | :---: | :---: | :--- |
| **handshake** | `H` | 16/16 (100.0%) | 14/14 (100.0%) | **30/30 (100.0%)** | Handshake completes successfully. |
| **transfer** | `DC` | 16/16 (100.0%) | 14/14 (100.0%) | **30/30 (100.0%)** | Stream data is being sent and received correctly. Connection close completes with a zero error code. |
| **longrtt** | `LR` | 16/16 (100.0%) | 14/14 (100.0%) | **30/30 (100.0%)** | Handshake completes when RTT is long. |
| **chacha20** | `C20` | 14/14 (100.0%) | 10/10 (100.0%) | **24/24 (100.0%)** | Handshake completes using ChaCha20. |
| **multiplexing** | `M` | 16/16 (100.0%) | 13/14 (92.9%) | **29/30 (96.7%)** | Thousands of files are transferred over a single connection, and server increased stream limits to accomodate client requests. |
| **retry** | `S` | 14/14 (100.0%) | 12/12 (100.0%) | **26/26 (100.0%)** | Server sends a Retry, and a subsequent connection using the Retry token completes successfully. |
| **resumption** | `R` | 14/15 (93.3%) | 12/12 (100.0%) | **26/27 (96.3%)** | Connection is established using TLS Session Resumption. |
| **zerortt** | `Z` | 14/14 (100.0%) | 10/12 (83.3%) | **24/26 (92.3%)** | 0-RTT data is being sent and acted on. |
| **http3** | `3` | 14/14 (100.0%) | 13/13 (100.0%) | **27/27 (100.0%)** | An H3 transaction succeeded. |
| **blackhole** | `B` | 16/16 (100.0%) | 14/14 (100.0%) | **30/30 (100.0%)** | Transfer succeeds despite underlying network blacking out for a few seconds. |
| **keyupdate** | `U` | 16/16 (100.0%) | 8/10 (80.0%) | **24/26 (92.3%)** | One of the two endpoints updates keys and the peer responds correctly. |
| **ecn** | `E` | 6/6 (100.0%) | 6/6 (100.0%) | **12/12 (100.0%)** | Handshake completes successfully. |
| **amplificationlimit** | `A` | 15/16 (93.8%) | 13/14 (92.9%) | **28/30 (93.3%)** | The server obeys the 3x amplification limit. |
| **handshakeloss** | `L1` | 15/15 (100.0%) | 12/13 (92.3%) | **27/28 (96.4%)** | Handshake completes under extreme packet loss. |
| **transferloss** | `L2` | 16/16 (100.0%) | 14/14 (100.0%) | **30/30 (100.0%)** | Transfer completes under moderate packet loss. |
| **handshakecorruption** | `C1` | 15/15 (100.0%) | 11/13 (84.6%) | **26/28 (92.9%)** | Handshake completes under extreme packet corruption. |
| **transfercorruption** | `C2` | 16/16 (100.0%) | 14/14 (100.0%) | **30/30 (100.0%)** | Transfer completes under moderate packet corruption. |
| **ipv6** | `6` | 16/16 (100.0%) | 14/14 (100.0%) | **30/30 (100.0%)** | A transfer across an IPv6-only network succeeded. |
| **v2** | `V2` | 8/8 (100.0%) | 7/8 (87.5%) | **15/16 (93.8%)** | Server should select QUIC v2 in compatible version negotiation. |
| **rebind-port** | `BP` | 10/16 (62.5%) | 13/14 (92.9%) | **23/30 (76.7%)** | Transfer completes under frequent port rebindings on the client side. |
| **rebind-addr** | `BA` | 8/16 (50.0%) | 14/14 (100.0%) | **22/30 (73.3%)** | Transfer completes under frequent IP address and port rebindings on the client side. |
| **connectionmigration** | `CM` | 3/5 (60.0%) | 4/14 (28.6%) | **7/19 (36.8%)** | A transfer succeeded during which the client performed an active migration. |
| **goodput** | `G` | 16/16 (100.0%) | 14/14 (100.0%) | **30/30 (100.0%)** | Measures connection goodput over a 10Mbps link. |
| **crosstraffic** | `C` | 8/16 (50.0%) | 13/14 (92.9%) | **21/30 (70.0%)** | Measures goodput over a 10Mbps link when competing with a TCP (cubic) connection. |

---

## 5. 对端实现互通率排行榜

| 排名 | 对端实现 | Client 模式 (发起) | Server 模式 (响应) | 协议功能通过率 | 综合有效通过率 | 状态 |
| :---: | :--- | :---: | :---: | :---: | :---: | :---: |
| 1 | **neqo** | 24/24 (100.0%) | 23/24 (95.8%) | **43/44 (97.7%)** | 47/48 (97.9%) | 🌟 优秀 |
| 2 | **picoquic** | 24/24 (100.0%) | 23/24 (95.8%) | **43/44 (97.7%)** | 47/48 (97.9%) | 🌟 优秀 |
| 3 | **lsquic** | 23/24 (95.8%) | 23/23 (100.0%) | **42/43 (97.7%)** | 46/47 (97.9%) | 🌟 优秀 |
| 4 | **aioquic** | 21/22 (95.5%) | 22/23 (95.7%) | **40/41 (97.6%)** | 43/45 (95.6%) | 🌟 优秀 |
| 5 | **msquic** | 21/21 (100.0%) | 20/22 (90.9%) | **37/39 (94.9%)** | 41/43 (95.3%) | 🌟 优秀 |
| 6 | **quic-go** | 20/21 (95.2%) | 20/22 (90.9%) | **37/39 (94.9%)** | 40/43 (93.0%) | 🌟 优秀 |
| 7 | **xquic** | 21/21 (100.0%) | 19/22 (86.4%) | **37/39 (94.9%)** | 40/43 (93.0%) | 🌟 优秀 |
| 8 | **ngtcp2** | 20/24 (83.3%) | 24/24 (100.0%) | **41/44 (93.2%)** | 44/48 (91.7%) | 🌟 优秀 |
| 9 | **quinn** | 22/22 (100.0%) | 21/24 (87.5%) | **39/42 (92.9%)** | 43/46 (93.5%) | 🌟 优秀 |
| 10 | **s2n-quic** | 20/22 (90.9%) | 19/20 (95.0%) | **35/38 (92.1%)** | 39/42 (92.9%) | 🌟 优秀 |
| 11 | **quiche** | 18/21 (85.7%) | 19/20 (95.0%) | **34/37 (91.9%)** | 37/41 (90.2%) | 🌟 优秀 |
| 12 | **kwik** | 19/22 (86.4%) | 21/23 (91.3%) | **37/41 (90.2%)** | 40/45 (88.9%) | 🌟 优秀 |
| 13 | **haproxy** | 20/22 (90.9%) | N/A | **18/20 (90.0%)** | 20/22 (90.9%) | 🌟 优秀 |
| 14 | **nginx** | 18/21 (85.7%) | N/A | **17/19 (89.5%)** | 18/21 (85.7%) | 👍 良好 |
| 15 | **go-x-net** | 11/14 (78.6%) | 14/15 (93.3%) | **22/25 (88.0%)** | 25/29 (86.2%) | 👍 良好 |
| 16 | **mvfst** | 16/19 (84.2%) | 14/18 (77.8%) | **27/33 (81.8%)** | 30/37 (81.1%) | 👍 良好 |
| 17 | **chrome** | N/A | 1/1 (100.0%) | **1/1 (100.0%)** | 1/1 (100.0%) | 🌟 优秀 |

> 注：排行榜中 Client 模式指 quicX 作为 Client 连接对端 Server，Server 模式指对端作为 Client 连接 quicX Server。"协议功能通过率"不含 goodput / crosstraffic 两个性能场景。

---

## 6. 按互通场景分类的详细矩阵

### 🔹 handshake (`H`)

> **说明**: Handshake completes successfully.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 transfer (`DC`)

> **说明**: Stream data is being sent and received correctly. Connection close completes with a zero error code.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 longrtt (`LR`)

> **说明**: Handshake completes when RTT is long.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 chacha20 (`C20`)

> **说明**: Handshake completes using ChaCha20.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ➖ | ✅ | ➖ | ➖ | ✅ | ✅ | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 multiplexing (`M`)

> **说明**: Thousands of files are transferred over a single connection, and server increased stream limits to accomodate client requests.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | — | — | ✅ | ➖ |

### 🔹 retry (`S`)

> **说明**: Server sends a Retry, and a subsequent connection using the Retry token completes successfully.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 resumption (`R`)

> **说明**: Connection is established using TLS Session Resumption.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 zerortt (`Z`)

> **说明**: 0-RTT data is being sent and acted on.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ➖ | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ❌ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 http3 (`3`)

> **说明**: An H3 transaction succeeded.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ➖ | ✅ | ✅ | — | — | ✅ | ✅ |

### 🔹 blackhole (`B`)

> **说明**: Transfer succeeds despite underlying network blacking out for a few seconds.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 keyupdate (`U`)

> **说明**: One of the two endpoints updates keys and the peer responds correctly.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ➖ | ➖ | ✅ | ➖ | ✅ | ➖ | ✅ | ✅ | ❌ | ✅ | — | — | ❌ | ➖ |

### 🔹 ecn (`E`)

> **说明**: Handshake completes successfully.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ➖ | ✅ | ➖ | ✅ | ✅ | ➖ | ➖ | ➖ | ➖ | ✅ | ➖ | ➖ | ➖ | ✅ | — |
| **quicX as Server** | ✅ | ➖ | ➖ | ✅ | ➖ | ✅ | ✅ | ➖ | ➖ | ➖ | ➖ | ✅ | ➖ | — | — | ✅ | ➖ |

### 🔹 amplificationlimit (`A`)

> **说明**: The server obeys the 3x amplification limit.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 handshakeloss (`L1`)

> **说明**: Handshake completes under extreme packet loss.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 transferloss (`L2`)

> **说明**: Transfer completes under moderate packet loss.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 handshakecorruption (`C1`)

> **说明**: Handshake completes under extreme packet corruption.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ➖ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 transfercorruption (`C2`)

> **说明**: Transfer completes under moderate packet corruption.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 ipv6 (`6`)

> **说明**: A transfer across an IPv6-only network succeeded.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 v2 (`V2`)

> **说明**: Server should select QUIC v2 in compatible version negotiation.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ➖ | ➖ | ➖ | ✅ | ✅ | ✅ | ➖ | ✅ | ✅ | ✅ | ➖ | ➖ | ✅ | ➖ | — |
| **quicX as Server** | ✅ | ➖ | ➖ | ➖ | ➖ | ✅ | ✅ | ✅ | ➖ | ✅ | ✅ | ✅ | ➖ | — | — | ❌ | ➖ |

### 🔹 rebind-port (`BP`)

> **说明**: Transfer completes under frequent port rebindings on the client side.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ❌ | ❌ | ✅ | ✅ | ❌ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | ❌ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 rebind-addr (`BA`)

> **说明**: Transfer completes under frequent IP address and port rebindings on the client side.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ❌ | ❌ | ✅ | ❌ | ❌ | ✅ | ✅ | ❌ | ❌ | ✅ | ✅ | ✅ | ✅ | ❌ | ❌ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 connectionmigration (`CM`)

> **说明**: A transfer succeeded during which the client performed an active migration.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ❌ | ➖ | ➖ | ❌ | ➖ | ✅ | ✅ | ➖ | ➖ | ➖ | ➖ | ✅ | ➖ | ➖ | ➖ | ➖ | — |
| **quicX as Server** | ✅ | ❌ | ❌ | ❌ | ❌ | ✅ | ✅ | ❌ | ❌ | ❌ | ❌ | ✅ | ❌ | — | — | ❌ | ➖ |

### 🔹 goodput (`G`)

> **说明**: Measures connection goodput over a 10Mbps link.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅<br><sub>9.31 Mbps</sub> | ✅<br><sub>8.07 Mbps</sub> | ✅<br><sub>9.51 Mbps</sub> | ✅<br><sub>9.26 Mbps</sub> | ✅<br><sub>9.45 Mbps</sub> | ✅<br><sub>8.41 Mbps</sub> | ✅<br><sub>9.55 Mbps</sub> | ✅<br><sub>8.76 Mbps</sub> | ✅<br><sub>9.35 Mbps</sub> | ✅<br><sub>8.64 Mbps</sub> | ✅<br><sub>8.59 Mbps</sub> | ✅<br><sub>9.27 Mbps</sub> | ✅<br><sub>9.36 Mbps</sub> | ✅<br><sub>9.47 Mbps</sub> | ✅<br><sub>9.15 Mbps</sub> | ✅<br><sub>8.76 Mbps</sub> | — |
| **quicX as Server** | ✅<br><sub>9.38 Mbps</sub> | ✅<br><sub>9.42 Mbps</sub> | ✅<br><sub>9.49 Mbps</sub> | ✅<br><sub>8.80 Mbps</sub> | ✅<br><sub>9.34 Mbps</sub> | ✅<br><sub>9.19 Mbps</sub> | ✅<br><sub>9.44 Mbps</sub> | ✅<br><sub>9.36 Mbps</sub> | ✅<br><sub>9.43 Mbps</sub> | ✅<br><sub>9.36 Mbps</sub> | ✅<br><sub>9.48 Mbps</sub> | ✅<br><sub>9.09 Mbps</sub> | ✅<br><sub>9.34 Mbps</sub> | — | — | ✅<br><sub>9.15 Mbps</sub> | ➖ |

#### 📊 实测吞吐速率排行榜 (10 Mbps 链路测试，平均速率: **9.17 Mbps**)

| 排名 | 对端实现 | 测试角色 | 状态 | 详细速率 (含波动) | 实测吞吐 (Mbps) | 带宽利用率 |
| :---: | :--- | :---: | :---: | :---: | :---: | :---: |
| 1 | **lsquic** | quicX as Client | ✅ | 9549 (± 11) kbps | **9.55 Mbps** | `95.5%` |
| 2 | **quic-go** | quicX as Client | ✅ | 9506 (± 1) kbps | **9.51 Mbps** | `95.1%` |
| 3 | **quic-go** | quicX as Server | ✅ | 9492 (± 15) kbps | **9.49 Mbps** | `94.9%` |
| 4 | **msquic** | quicX as Server | ✅ | 9481 (± 12) kbps | **9.48 Mbps** | `94.8%` |
| 5 | **nginx** | quicX as Client | ✅ | 9471 (± 1) kbps | **9.47 Mbps** | `94.7%` |
| 6 | **quiche** | quicX as Client | ✅ | 9447 (± 4) kbps | **9.45 Mbps** | `94.5%` |
| 7 | **lsquic** | quicX as Server | ✅ | 9438 (± 6) kbps | **9.44 Mbps** | `94.4%` |
| 8 | **mvfst** | quicX as Server | ✅ | 9429 (± 25) kbps | **9.43 Mbps** | `94.3%` |
| 9 | **go-x-net** | quicX as Server | ✅ | 9416 (± 11) kbps | **9.42 Mbps** | `94.2%` |
| 10 | **ngtcp2** | quicX as Server | ✅ | 9376 (± 13) kbps | **9.38 Mbps** | `93.8%` |
| 11 | **xquic** | quicX as Client | ✅ | 9362 (± 5) kbps | **9.36 Mbps** | `93.6%` |
| 12 | **aioquic** | quicX as Server | ✅ | 9361 (± 17) kbps | **9.36 Mbps** | `93.6%` |
| 13 | **kwik** | quicX as Server | ✅ | 9358 (± 21) kbps | **9.36 Mbps** | `93.6%` |
| 14 | **mvfst** | quicX as Client | ✅ | 9354 (± 29) kbps | **9.35 Mbps** | `93.5%` |
| 15 | **xquic** | quicX as Server | ✅ | 9341 (± 91) kbps | **9.34 Mbps** | `93.4%` |
| 16 | **quiche** | quicX as Server | ✅ | 9337 (± 12) kbps | **9.34 Mbps** | `93.4%` |
| 17 | **ngtcp2** | quicX as Client | ✅ | 9307 (± 91) kbps | **9.31 Mbps** | `93.1%` |
| 18 | **picoquic** | quicX as Client | ✅ | 9273 (± 51) kbps | **9.27 Mbps** | `92.7%` |
| 19 | **s2n-quic** | quicX as Client | ✅ | 9264 (± 37) kbps | **9.26 Mbps** | `92.6%` |
| 20 | **neqo** | quicX as Server | ✅ | 9194 (± 18) kbps | **9.19 Mbps** | `91.9%` |
| 21 | **quinn** | quicX as Server | ✅ | 9149 (± 18) kbps | **9.15 Mbps** | `91.5%` |
| 22 | **haproxy** | quicX as Client | ✅ | 9147 (± 15) kbps | **9.15 Mbps** | `91.5%` |
| 23 | **picoquic** | quicX as Server | ✅ | 9093 (± 31) kbps | **9.09 Mbps** | `90.9%` |
| 24 | **s2n-quic** | quicX as Server | ✅ | 8803 (± 74) kbps | **8.80 Mbps** | `88.0%` |
| 25 | **quinn** | quicX as Client | ✅ | 8761 (± 6) kbps | **8.76 Mbps** | `87.6%` |
| 26 | **kwik** | quicX as Client | ✅ | 8756 (± 30) kbps | **8.76 Mbps** | `87.6%` |
| 27 | **aioquic** | quicX as Client | ✅ | 8640 (± 21) kbps | **8.64 Mbps** | `86.4%` |
| 28 | **msquic** | quicX as Client | ✅ | 8586 (± 42) kbps | **8.59 Mbps** | `85.9%` |
| 29 | **neqo** | quicX as Client | ✅ | 8409 (± 28) kbps | **8.41 Mbps** | `84.1%` |
| 30 | **go-x-net** | quicX as Client | ✅ | 8069 (± 14) kbps | **8.07 Mbps** | `80.7%` |

> 💡 **性能分析**: quicX 在 10Mbps 瓶颈链路上表现极佳，双向全部 30 个实测场景（Client 模式 16/16，Server 模式 14/14）**100% 满分通过**，最高带宽利用率达 **95.5%**（`lsquic`: 9.55 Mbps），全局平均有效速率达到 **9.17 Mbps**。

### 🔹 crosstraffic (`C`)

> **说明**: Measures goodput over a 10Mbps link when competing with a TCP (cubic) connection.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ❌ | ❌ | ❌ | ✅<br><sub>4.84 Mbps</sub> | ❌ | ✅<br><sub>4.04 Mbps</sub> | ✅<br><sub>8.36 Mbps</sub> | ❌ | ❌ | ❌ | ✅<br><sub>3.28 Mbps</sub> | ✅<br><sub>7.20 Mbps</sub> | ✅<br><sub>8.57 Mbps</sub> | ❌ | ✅<br><sub>4.75 Mbps</sub> | ✅<br><sub>3.03 Mbps</sub> | — |
| **quicX as Server** | ✅<br><sub>4.13 Mbps</sub> | ✅<br><sub>4.81 Mbps</sub> | ✅<br><sub>4.95 Mbps</sub> | ✅<br><sub>4.66 Mbps</sub> | ✅<br><sub>5.46 Mbps</sub> | ✅<br><sub>4.70 Mbps</sub> | ✅<br><sub>5.15 Mbps</sub> | ✅<br><sub>5.69 Mbps</sub> | ✅<br><sub>6.16 Mbps</sub> | ✅<br><sub>7.19 Mbps</sub> | ✅<br><sub>6.45 Mbps</sub> | ✅<br><sub>4.87 Mbps</sub> | ❌ | — | — | ✅<br><sub>5.33 Mbps</sub> | ➖ |

#### 📊 与 TCP (Cubic) 竞争实测数据明细

| 排名 | 对端实现 | 测试角色 | 状态 | 详细速率 (含波动) | 实测吞吐 (Mbps) | 说明 |
| :---: | :--- | :---: | :---: | :---: | :---: | :--- |
| 1 | **xquic** | quicX as Client | ✅ | 8570 (± 51) kbps | **8.57 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `85.7%`) |
| 2 | **lsquic** | quicX as Client | ✅ | 8361 (± 158) kbps | **8.36 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `83.6%`) |
| 3 | **picoquic** | quicX as Client | ✅ | 7203 (± 264) kbps | **7.20 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `72.0%`) |
| 4 | **aioquic** | quicX as Server | ✅ | 7194 (± 451) kbps | **7.19 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `71.9%`) |
| 5 | **msquic** | quicX as Server | ✅ | 6445 (± 446) kbps | **6.45 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `64.5%`) |
| 6 | **mvfst** | quicX as Server | ✅ | 6164 (± 223) kbps | **6.16 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `61.6%`) |
| 7 | **kwik** | quicX as Server | ✅ | 5687 (± 230) kbps | **5.69 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `56.9%`) |
| 8 | **quiche** | quicX as Server | ✅ | 5460 (± 556) kbps | **5.46 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `54.6%`) |
| 9 | **quinn** | quicX as Server | ✅ | 5328 (± 596) kbps | **5.33 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `53.3%`) |
| 10 | **lsquic** | quicX as Server | ✅ | 5150 (± 204) kbps | **5.15 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `51.5%`) |
| 11 | **quic-go** | quicX as Server | ✅ | 4953 (± 544) kbps | **4.95 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `49.5%`) |
| 12 | **picoquic** | quicX as Server | ✅ | 4865 (± 293) kbps | **4.87 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `48.6%`) |
| 13 | **s2n-quic** | quicX as Client | ✅ | 4845 (± 191) kbps | **4.84 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `48.4%`) |
| 14 | **go-x-net** | quicX as Server | ✅ | 4812 (± 59) kbps | **4.81 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `48.1%`) |
| 15 | **haproxy** | quicX as Client | ✅ | 4746 (± 158) kbps | **4.75 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `47.5%`) |
| 16 | **neqo** | quicX as Server | ✅ | 4701 (± 496) kbps | **4.70 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `47.0%`) |
| 17 | **s2n-quic** | quicX as Server | ✅ | 4661 (± 332) kbps | **4.66 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `46.6%`) |
| 18 | **ngtcp2** | quicX as Server | ✅ | 4127 (± 588) kbps | **4.13 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `41.3%`) |
| 19 | **neqo** | quicX as Client | ✅ | 4041 (± 172) kbps | **4.04 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `40.4%`) |
| 20 | **msquic** | quicX as Client | ✅ | 3282 (± 78) kbps | **3.28 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `32.8%`) |
| 21 | **quinn** | quicX as Client | ✅ | 3030 (± 339) kbps | **3.03 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `30.3%`) |
| - | `ngtcp2`, `go-x-net`, `quic-go`, `quiche`, `kwik`, `mvfst`, `aioquic`, `nginx` | quicX as Client | ❌ | — | — | 与 TCP Cubic 竞争时测算未达 interop runner 阈值 |
| - | `xquic` | quicX as Server | ❌ | — | — | 与 TCP Cubic 竞争时测算未达 interop runner 阈值 |

> 💡 **竞争分析**: quicX 作为 Server 时展现出稳定的抗拥塞与带宽抢占能力，14 个对端 Client 中 **13/14 (92.9%)** 达标（`aioquic` 7.19 Mbps、`msquic` 6.45 Mbps、`mvfst` 6.16 Mbps 表现最佳），仅 `xquic` 未达阈值；作为 Client 时 **8/16 (50.0%)** 达标（`xquic` 8.57 Mbps、`lsquic` 8.36 Mbps、`picoquic` 7.20 Mbps 抢占效率最高），`ngtcp2`、`go-x-net`、`quic-go`、`quiche`、`kwik`、`mvfst`、`aioquic`、`nginx` 八个组合未达阈值。综合抗 TCP Cubic 竞争达标率 **70.0% (21/30)**，Client 模式抗竞争能力为本轮主要短板。
