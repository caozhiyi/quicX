# quicX 互操作性测试报告

本文档记录 quicX 与主流 QUIC 实现互操作测试的**最新基线结果**，可作为对外引用版本。

测试方法、命令与环境依赖见 [`guide/interop_runbook.md`](../guide/interop_runbook.md)。

| 项 | 值 |
|---|---|
| **运行模式** | 上游 quic-interop-runner 全场景矩阵（quic-network-simulator / ns-3 拓扑） |
| **被测对端数** | 17 个第三方实现（14 双向 + nginx / haproxy 仅 Server + chrome 仅 Client） |
| **被测场景数** | 24 个（22 个协议功能场景 + 2 个性能测算场景） |
| **总用例数** | 744（PASS 588 / FAIL 61 / UNSUPPORTED 95） |
| **综合有效通过率** | **90.60%**（588 / 649，剔除 UNSUPPORTED） |
| **协议功能合规通过率** | **90.15%**（531 / 589；Client 模式 292/312 = 93.59%，Server 模式 239/277 = 86.28%） |
| **性能测算通过率** | **95.00%**（57 / 60） |

---

## 总体情况

- ✅ 24 场景 × 17 对端全矩阵（744 用例）下，quicX **综合有效通过率 90.60%**；
- ✅ **12 个场景 100% 通过**：`handshake`、`transfer`、`longrtt`、`chacha20`、`retry`、`http3`、`blackhole`、`ecn`、`transferloss`、`transfercorruption`、`ipv6`，以及 `goodput`（30/30 满分，平均 8.67 Mbps，最高带宽利用率 94.7%）；
- ✅ `crosstraffic`（与 TCP Cubic 竞争）27/30 达标（90.0%），仅 kwik / mvfst（Client）与 xquic（Server）未达阈值；
- ✅ 与上轮（91.22%）相比：`quicx → mvfst` `retry`、`quicx → quic-go` `http3`、`quicx → picoquic` `blackhole`、`quicx → mvfst` `transfercorruption` 恢复通过，`zerortt` Client 模式与 Server 模式 `handshakeloss` / `handshakecorruption` 明显改善；
- ⚠️ 当前薄弱项：`connectionmigration`（31.6%）、`rebind-addr`（60.0%）、`rebind-port` / `amplificationlimit`（各 66.7%，其中 amplificationlimit Server 模式仅 6/14，为本轮最主要回归）、Server 模式 `keyupdate`（8/10）。

---

## 1. 测试概述

- **数据来源**：上游 [quic-interop-runner](https://github.com/quic-interop/quic-interop-runner) 全场景矩阵一轮完整运行。
- **运行环境**：Linux 宿主，Docker + ns-3 网络仿真（leftnet ↔ sim ↔ rightnet），quicX 使用本地构建的 `interop_{server,client}`。
- **测试范围**：24 场景 × (16 个 Server 对端 + 15 个 Client 对端) = **744 个组合**，无 quicX ↔ quicX 自测项。
- **与前基线差异**：上轮为相同的 24 场景 × 17 对端口径（744 用例，有效通过率 91.22%），两轮直接可比。本轮综合通过率下降 0.62pp，主要来自 Server 模式 `amplificationlimit`（14/14 → 6/14）、`keyupdate`（9/10 → 8/10）与 `xquic` Server `multiplexing` 等新增失败；同时 `retry`（25/26 → 26/26）、`http3`（26/27 → 27/27）、`blackhole`（29/30 → 30/30）、`transfercorruption`（29/30 → 30/30）恢复满分，`zerortt`（23/26 → 24/26）、`handshakecorruption`（24/28 → 26/28）改善。上游矩阵不包含 `multiconnect` / `versionnegotiation`。

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
| 总用例数 | **744** |
| ✅ 成功 | **588** |
| ❌ 失败 | **61** |
| ➖ 不支持 | **95** |
| **综合有效通过率（剔除 Unsupported）** | **588 / 649 ≈ 90.60%** |
| 协议功能合规通过率 | 531 / 589 ≈ 90.15%（Client 模式 292/312 = 93.59%；Server 模式 239/277 = 86.28%） |
| 性能测算通过率 | 57 / 60 ≈ 95.00%（Goodput 30/30；CrossTraffic 27/30） |
| 含 Unsupported 通过率 | 588 / 744 ≈ 79.0% |

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
| **amplificationlimit** | `A` | 14/16 (87.5%) | 6/14 (42.9%) | **20/30 (66.7%)** | The server obeys the 3x amplification limit. |
| **handshakeloss** | `L1` | 14/15 (93.3%) | 10/13 (76.9%) | **24/28 (85.7%)** | Handshake completes under extreme packet loss. |
| **transferloss** | `L2` | 16/16 (100.0%) | 14/14 (100.0%) | **30/30 (100.0%)** | Transfer completes under moderate packet loss. |
| **handshakecorruption** | `C1` | 15/15 (100.0%) | 11/13 (84.6%) | **26/28 (92.9%)** | Handshake completes under extreme packet corruption. |
| **transfercorruption** | `C2` | 16/16 (100.0%) | 14/14 (100.0%) | **30/30 (100.0%)** | Transfer completes under moderate packet corruption. |
| **ipv6** | `6` | 16/16 (100.0%) | 14/14 (100.0%) | **30/30 (100.0%)** | A transfer across an IPv6-only network succeeded. |
| **v2** | `V2` | 8/8 (100.0%) | 7/8 (87.5%) | **15/16 (93.8%)** | Server should select QUIC v2 in compatible version negotiation. |
| **rebind-port** | `BP` | 10/16 (62.5%) | 10/14 (71.4%) | **20/30 (66.7%)** | Transfer completes under frequent port rebindings on the client side. |
| **rebind-addr** | `BA` | 8/16 (50.0%) | 10/14 (71.4%) | **18/30 (60.0%)** | Transfer completes under frequent IP address and port rebindings on the client side. |
| **connectionmigration** | `CM` | 3/5 (60.0%) | 3/14 (21.4%) | **6/19 (31.6%)** | A transfer succeeded during which the client performed an active migration. |
| **goodput** | `G` | 16/16 (100.0%) | 14/14 (100.0%) | **30/30 (100.0%)** | Measures connection goodput over a 10Mbps link. |
| **crosstraffic** | `C` | 14/16 (87.5%) | 13/14 (92.9%) | **27/30 (90.0%)** | Measures goodput over a 10Mbps link when competing with a TCP (cubic) connection. |

---

## 5. 对端实现互通率排行榜

| 排名 | 对端实现 | Client 模式 (发起) | Server 模式 (响应) | 协议功能通过率 | 综合有效通过率 | 状态 |
| :---: | :--- | :---: | :---: | :---: | :---: | :---: |
| 1 | **aioquic** | 22/22 (100.0%) | 22/23 (95.7%) | **40/41 (97.6%)** | 44/45 (97.8%) | 🌟 优秀 |
| 2 | **picoquic** | 24/24 (100.0%) | 22/24 (91.7%) | **42/44 (95.5%)** | 46/48 (95.8%) | 🌟 优秀 |
| 3 | **lsquic** | 23/24 (95.8%) | 22/23 (95.7%) | **41/43 (95.3%)** | 45/47 (95.7%) | 🌟 优秀 |
| 4 | **ngtcp2** | 21/24 (87.5%) | 24/24 (100.0%) | **41/44 (93.2%)** | 45/48 (93.8%) | 🌟 优秀 |
| 5 | **kwik** | 19/22 (86.4%) | 22/23 (95.7%) | **38/41 (92.7%)** | 41/45 (91.1%) | 🌟 优秀 |
| 6 | **neqo** | 24/24 (100.0%) | 20/24 (83.3%) | **40/44 (90.9%)** | 44/48 (91.7%) | 🌟 优秀 |
| 7 | **quinn** | 22/22 (100.0%) | 20/24 (83.3%) | **38/42 (90.5%)** | 42/46 (91.3%) | 🌟 优秀 |
| 8 | **haproxy** | 20/22 (90.9%) | N/A | **18/20 (90.0%)** | 20/22 (90.9%) | 🌟 优秀 |
| 9 | **quic-go** | 21/21 (100.0%) | 18/22 (81.8%) | **35/39 (89.7%)** | 39/43 (90.7%) | 👍 良好 |
| 10 | **msquic** | 21/21 (100.0%) | 18/22 (81.8%) | **35/39 (89.7%)** | 39/43 (90.7%) | 👍 良好 |
| 11 | **s2n-quic** | 20/22 (90.9%) | 18/20 (90.0%) | **34/38 (89.5%)** | 38/42 (90.5%) | 👍 良好 |
| 12 | **nginx** | 19/21 (90.5%) | N/A | **17/19 (89.5%)** | 19/21 (90.5%) | 👍 良好 |
| 13 | **quiche** | 19/21 (90.5%) | 18/20 (90.0%) | **33/37 (89.2%)** | 37/41 (90.2%) | 👍 良好 |
| 14 | **xquic** | 19/21 (90.5%) | 17/22 (77.3%) | **33/39 (84.6%)** | 36/43 (83.7%) | 👍 良好 |
| 15 | **go-x-net** | 12/14 (85.7%) | 13/15 (86.7%) | **21/25 (84.0%)** | 25/29 (86.2%) | 👍 良好 |
| 16 | **mvfst** | 16/19 (84.2%) | 11/18 (61.1%) | **24/33 (72.7%)** | 27/37 (73.0%) | ⚠️ 待提升 |
| 17 | **chrome** | N/A | 1/1 (100.0%) | **1/1 (100.0%)** | 1/1 (100.0%) | 🌟 优秀 |

> 注：排行榜中 Client 模式指 quicX 作为 Client 连接对端 Server，Server 模式指对端作为 Client 连接 quicX Server。"协议功能通过率"不含 goodput / crosstraffic 两个性能场景。

---

## 6. 按互通场景分类的详细矩阵

## 📋 按互通场景分类的详细矩阵表格

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
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ❌ | ❌ | ❌ | ✅ | ❌ | ❌ | ✅ | ❌ | ✅ | ✅ | ❌ | ✅ | — | — | ❌ | ➖ |



### 🔹 handshakeloss (`L1`)

> **说明**: Handshake completes under extreme packet loss.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ➖ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ❌ | — | — | ✅ | ➖ |



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
| **quicX as Server** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ❌ | — | — | ✅ | ➖ |



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
| **quicX as Server** | ✅ | ✅ | ❌ | ✅ | ✅ | ❌ | ✅ | ✅ | ❌ | ✅ | ❌ | ✅ | ✅ | — | — | ✅ | ➖ |



### 🔹 rebind-addr (`BA`)

> **说明**: Transfer completes under frequent IP address and port rebindings on the client side.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ❌ | ❌ | ✅ | ❌ | ❌ | ✅ | ✅ | ❌ | ❌ | ✅ | ✅ | ✅ | ✅ | ❌ | ❌ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ❌ | ✅ | ✅ | ❌ | ✅ | ✅ | ❌ | ✅ | ❌ | ✅ | ✅ | — | — | ✅ | ➖ |



### 🔹 connectionmigration (`CM`)

> **说明**: A transfer succeeded during which the client performed an active migration.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ❌ | ➖ | ➖ | ❌ | ➖ | ✅ | ✅ | ➖ | ➖ | ➖ | ➖ | ✅ | ➖ | ➖ | ➖ | ➖ | — |
| **quicX as Server** | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ | ✅ | ❌ | ❌ | ❌ | ❌ | ✅ | ❌ | — | — | ❌ | ➖ |



### 🔹 goodput (`G`)

> **说明**: Measures connection goodput over a 10Mbps link.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅<br><sub>9.30 Mbps</sub> | ✅<br><sub>7.97 Mbps</sub> | ✅<br><sub>9.43 Mbps</sub> | ✅<br><sub>9.19 Mbps</sub> | ✅<br><sub>9.35 Mbps</sub> | ✅<br><sub>8.35 Mbps</sub> | ✅<br><sub>9.47 Mbps</sub> | ✅<br><sub>7.97 Mbps</sub> | ✅<br><sub>9.26 Mbps</sub> | ✅<br><sub>8.55 Mbps</sub> | ✅<br><sub>8.54 Mbps</sub> | ✅<br><sub>9.17 Mbps</sub> | ✅<br><sub>9.26 Mbps</sub> | ✅<br><sub>9.40 Mbps</sub> | ✅<br><sub>8.97 Mbps</sub> | ✅<br><sub>8.69 Mbps</sub> | — |
| **quicX as Server** | ✅<br><sub>9.20 Mbps</sub> | ✅<br><sub>9.38 Mbps</sub> | ✅<br><sub>8.17 Mbps</sub> | ✅<br><sub>8.77 Mbps</sub> | ✅<br><sub>6.81 Mbps</sub> | ✅<br><sub>8.70 Mbps</sub> | ✅<br><sub>8.88 Mbps</sub> | ✅<br><sub>7.11 Mbps</sub> | ✅<br><sub>6.82 Mbps</sub> | ✅<br><sub>9.20 Mbps</sub> | ✅<br><sub>8.23 Mbps</sub> | ✅<br><sub>8.59 Mbps</sub> | ✅<br><sub>9.03 Mbps</sub> | — | — | ✅<br><sub>8.46 Mbps</sub> | ➖ |


#### 📊 实测吞吐速率排行榜 (10 Mbps 链路测试，平均速率: **8.67 Mbps**)

| 排名 | 对端实现 | 测试角色 | 状态 | 详细速率 (含波动) | 实测吞吐 (Mbps) | 带宽利用率 |
| :---: | :--- | :---: | :---: | :---: | :---: | :---: |
| 1 | **lsquic** | quicX as Client | ✅ | 9470 (± 9) kbps | **9.47 Mbps** | `94.7%` |
| 2 | **quic-go** | quicX as Client | ✅ | 9426 (± 1) kbps | **9.43 Mbps** | `94.3%` |
| 3 | **nginx** | quicX as Client | ✅ | 9404 (± 8) kbps | **9.40 Mbps** | `94.0%` |
| 4 | **go-x-net** | quicX as Server | ✅ | 9377 (± 17) kbps | **9.38 Mbps** | `93.8%` |
| 5 | **quiche** | quicX as Client | ✅ | 9351 (± 19) kbps | **9.35 Mbps** | `93.5%` |
| 6 | **ngtcp2** | quicX as Client | ✅ | 9299 (± 93) kbps | **9.30 Mbps** | `93.0%` |
| 7 | **xquic** | quicX as Client | ✅ | 9263 (± 5) kbps | **9.26 Mbps** | `92.6%` |
| 8 | **mvfst** | quicX as Client | ✅ | 9256 (± 45) kbps | **9.26 Mbps** | `92.6%` |
| 9 | **aioquic** | quicX as Server | ✅ | 9204 (± 85) kbps | **9.20 Mbps** | `92.0%` |
| 10 | **ngtcp2** | quicX as Server | ✅ | 9198 (± 71) kbps | **9.20 Mbps** | `92.0%` |
| 11 | **s2n-quic** | quicX as Client | ✅ | 9194 (± 40) kbps | **9.19 Mbps** | `91.9%` |
| 12 | **picoquic** | quicX as Client | ✅ | 9170 (± 61) kbps | **9.17 Mbps** | `91.7%` |
| 13 | **xquic** | quicX as Server | ✅ | 9029 (± 272) kbps | **9.03 Mbps** | `90.3%` |
| 14 | **haproxy** | quicX as Client | ✅ | 8970 (± 53) kbps | **8.97 Mbps** | `89.7%` |
| 15 | **lsquic** | quicX as Server | ✅ | 8884 (± 134) kbps | **8.88 Mbps** | `88.8%` |
| 16 | **s2n-quic** | quicX as Server | ✅ | 8774 (± 142) kbps | **8.77 Mbps** | `87.7%` |
| 17 | **neqo** | quicX as Server | ✅ | 8695 (± 164) kbps | **8.70 Mbps** | `87.0%` |
| 18 | **quinn** | quicX as Client | ✅ | 8691 (± 6) kbps | **8.69 Mbps** | `86.9%` |
| 19 | **picoquic** | quicX as Server | ✅ | 8589 (± 196) kbps | **8.59 Mbps** | `85.9%` |
| 20 | **aioquic** | quicX as Client | ✅ | 8554 (± 17) kbps | **8.55 Mbps** | `85.5%` |
| 21 | **msquic** | quicX as Client | ✅ | 8539 (± 38) kbps | **8.54 Mbps** | `85.4%` |
| 22 | **quinn** | quicX as Server | ✅ | 8463 (± 176) kbps | **8.46 Mbps** | `84.6%` |
| 23 | **neqo** | quicX as Client | ✅ | 8353 (± 13) kbps | **8.35 Mbps** | `83.5%` |
| 24 | **msquic** | quicX as Server | ✅ | 8235 (± 300) kbps | **8.23 Mbps** | `82.3%` |
| 25 | **quic-go** | quicX as Server | ✅ | 8168 (± 473) kbps | **8.17 Mbps** | `81.7%` |
| 26 | **go-x-net** | quicX as Client | ✅ | 7972 (± 15) kbps | **7.97 Mbps** | `79.7%` |
| 27 | **kwik** | quicX as Client | ✅ | 7966 (± 1550) kbps | **7.97 Mbps** | `79.7%` |
| 28 | **kwik** | quicX as Server | ✅ | 7108 (± 130) kbps | **7.11 Mbps** | `71.1%` |
| 29 | **mvfst** | quicX as Server | ✅ | 6823 (± 351) kbps | **6.82 Mbps** | `68.2%` |
| 30 | **quiche** | quicX as Server | ✅ | 6810 (± 128) kbps | **6.81 Mbps** | `68.1%` |

> 💡 **性能分析**: quicX 在 10Mbps 瓶颈链路上表现极佳，双向全部 30 个实测场景（Client 模式 16/16，Server 模式 14/14）**100% 满分通过**，最高带宽利用率达 **94.7%** (`lsquic`: 9.47 Mbps)，全局平均有效速率达到 **8.67 Mbps**。


### 🔹 crosstraffic (`C`)

> **说明**: Measures goodput over a 10Mbps link when competing with a TCP (cubic) connection.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅<br><sub>4.38 Mbps</sub> | ✅<br><sub>2.69 Mbps</sub> | ✅<br><sub>4.58 Mbps</sub> | ✅<br><sub>5.08 Mbps</sub> | ✅<br><sub>4.74 Mbps</sub> | ✅<br><sub>4.03 Mbps</sub> | ✅<br><sub>8.42 Mbps</sub> | ❌ | ❌ | ✅<br><sub>3.13 Mbps</sub> | ✅<br><sub>3.31 Mbps</sub> | ✅<br><sub>7.26 Mbps</sub> | ✅<br><sub>8.56 Mbps</sub> | ✅<br><sub>2.42 Mbps</sub> | ✅<br><sub>4.62 Mbps</sub> | ✅<br><sub>3.12 Mbps</sub> | — |
| **quicX as Server** | ✅<br><sub>9.06 Mbps</sub> | ✅<br><sub>6.11 Mbps</sub> | ✅<br><sub>8.41 Mbps</sub> | ✅<br><sub>8.62 Mbps</sub> | ✅<br><sub>6.71 Mbps</sub> | ✅<br><sub>8.77 Mbps</sub> | ✅<br><sub>8.77 Mbps</sub> | ✅<br><sub>6.58 Mbps</sub> | ✅<br><sub>6.64 Mbps</sub> | ✅<br><sub>8.96 Mbps</sub> | ✅<br><sub>8.55 Mbps</sub> | ✅<br><sub>8.50 Mbps</sub> | ❌ | — | — | ✅<br><sub>8.10 Mbps</sub> | ➖ |


#### 📊 与 TCP (Cubic) 竞争实测数据明细

| 排名 | 对端实现 | 测试角色 | 状态 | 详细速率 (含波动) | 实测吞吐 (Mbps) | 说明 |
| :---: | :--- | :---: | :---: | :---: | :---: | :--- |
| 1 | **ngtcp2** | quicX as Server | ✅ | 9058 (± 129) kbps | **9.06 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `90.6%`) |
| 2 | **aioquic** | quicX as Server | ✅ | 8960 (± 84) kbps | **8.96 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `89.6%`) |
| 3 | **lsquic** | quicX as Server | ✅ | 8770 (± 168) kbps | **8.77 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `87.7%`) |
| 4 | **neqo** | quicX as Server | ✅ | 8765 (± 268) kbps | **8.77 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `87.6%`) |
| 5 | **s2n-quic** | quicX as Server | ✅ | 8621 (± 68) kbps | **8.62 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `86.2%`) |
| 6 | **xquic** | quicX as Client | ✅ | 8564 (± 116) kbps | **8.56 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `85.6%`) |
| 7 | **msquic** | quicX as Server | ✅ | 8548 (± 273) kbps | **8.55 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `85.5%`) |
| 8 | **picoquic** | quicX as Server | ✅ | 8503 (± 137) kbps | **8.50 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `85.0%`) |
| 9 | **lsquic** | quicX as Client | ✅ | 8423 (± 125) kbps | **8.42 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `84.2%`) |
| 10 | **quic-go** | quicX as Server | ✅ | 8414 (± 97) kbps | **8.41 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `84.1%`) |
| 11 | **quinn** | quicX as Server | ✅ | 8097 (± 226) kbps | **8.10 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `81.0%`) |
| 12 | **picoquic** | quicX as Client | ✅ | 7256 (± 129) kbps | **7.26 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `72.6%`) |
| 13 | **quiche** | quicX as Server | ✅ | 6706 (± 310) kbps | **6.71 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `67.1%`) |
| 14 | **mvfst** | quicX as Server | ✅ | 6642 (± 163) kbps | **6.64 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `66.4%`) |
| 15 | **kwik** | quicX as Server | ✅ | 6578 (± 108) kbps | **6.58 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `65.8%`) |
| 16 | **go-x-net** | quicX as Server | ✅ | 6106 (± 107) kbps | **6.11 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `61.1%`) |
| 17 | **s2n-quic** | quicX as Client | ✅ | 5076 (± 152) kbps | **5.08 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `50.8%`) |
| 18 | **quiche** | quicX as Client | ✅ | 4737 (± 481) kbps | **4.74 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `47.4%`) |
| 19 | **haproxy** | quicX as Client | ✅ | 4620 (± 77) kbps | **4.62 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `46.2%`) |
| 20 | **quic-go** | quicX as Client | ✅ | 4582 (± 191) kbps | **4.58 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `45.8%`) |
| 21 | **ngtcp2** | quicX as Client | ✅ | 4378 (± 746) kbps | **4.38 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `43.8%`) |
| 22 | **neqo** | quicX as Client | ✅ | 4033 (± 87) kbps | **4.03 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `40.3%`) |
| 23 | **msquic** | quicX as Client | ✅ | 3315 (± 70) kbps | **3.31 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `33.1%`) |
| 24 | **aioquic** | quicX as Client | ✅ | 3133 (± 157) kbps | **3.13 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `31.3%`) |
| 25 | **quinn** | quicX as Client | ✅ | 3125 (± 123) kbps | **3.12 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `31.2%`) |
| 26 | **go-x-net** | quicX as Client | ✅ | 2694 (± 94) kbps | **2.69 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `26.9%`) |
| 27 | **nginx** | quicX as Client | ✅ | 2416 (± 99) kbps | **2.42 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `24.2%`) |
| - | `kwik`, `mvfst` | quicX as Client | ❌ | — | — | 与 TCP Cubic 竞争时测算未达 interop runner 阈值 |
| - | `xquic` | quicX as Server | ❌ | — | — | 与 TCP Cubic 竞争时测算未达 interop runner 阈值 |

> 💡 **竞争分析**: quicX 展现出双向均衡的抗 TCP Cubic 竞争能力：作为 Server 时在 14 个对端 Client 中成功拿下 **13/14 (92.9%)**，平均抢占速率 > 8.0 Mbps（对 ngtcp2 突破 9.0 Mbps，aioquic 达 8.96 Mbps）；作为 Client 时 **14/16 (87.5%)** 达标，其中对 xquic (8.56 Mbps) 与 lsquic (8.42 Mbps) 场景抢占效率最高。综合抗 TCP Cubic 竞争达标率达到 **90.0% (27/30)**，仅 kwik、mvfst (Client) 与 xquic (Server) 三个组合未达标。
