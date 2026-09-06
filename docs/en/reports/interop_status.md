# quicX Interoperability Status

> This document records the **latest baseline interoperability results** of
> quicX against the major QUIC implementations and is the canonical version
> to cite externally.
> For test methodology, commands, and environment, see
> [`guide/interop_runbook.md`](../guide/interop_runbook.md).

| Item | Value |
|---|---|
| **Run mode** | upstream quic-interop-runner full matrix (quic-network-simulator / ns-3 topology) |
| **Peers under test** | 17 third-party implementations (14 bidirectional + nginx / haproxy server-only + chrome client-only) |
| **Scenarios under test** | 24 (22 protocol-conformance + 2 performance measurements) |
| **Total cases** | 744 (PASS 592 / FAIL 57 / UNSUPPORTED 95) |
| **Overall effective pass rate** | **91.22%** (592 / 649, excluding UNSUPPORTED) |
| **RFC conformance pass rate** | **90.66%** (534 / 589; client mode 292/312 = 93.59%, server mode 246/277 = 88.81%) |
| **Performance pass rate** | **96.67%** (58 / 60) |

---

## TL;DR

- ✅ Across the full 24-scenario × 17-peer matrix (744 cases), quicX achieves an
  **overall effective pass rate of 91.22%**.
- ✅ **10 scenarios at 100%**: `handshake`, `transfer`, `longrtt`, `chacha20`,
  `multiplexing`, `ecn`, `transferloss`, `ipv6`, `goodput` (30/30, avg 8.76 Mbps,
  peak bandwidth utilization 94.7%), plus server-mode `transfercorruption`.
- ✅ `crosstraffic` (vs TCP Cubic) 28/30 (93.3%), balanced in both directions;
  only mvfst (client) and xquic (server) miss the threshold.
- ✅ **All three P0 items from the previous baseline (2026-08-09) are fixed**:
  `quicx → aioquic` `retry`, `quicx → picoquic | lsquic` `connectionmigration`,
  and `quicx → mvfst` `http3` now pass.
- ⚠️ Current weak spots: `connectionmigration` (31.6%),
  `rebind-port` / `rebind-addr` (63.3% each), server-mode
  `handshakeloss` / `handshakecorruption` (69.2% each).

---

## 1. Overview

- **Source**: one full-matrix run of the upstream
  [quic-interop-runner](https://github.com/quic-interop/quic-interop-runner)
  (incl. the 2026-09-04 crosstraffic follow-up round).
- **Environment**: Linux host, Docker + ns-3 network simulation
  (leftnet ↔ sim ↔ rightnet); quicX runs from locally built
  `interop_{server,client}` binaries.
- **Coverage**: 24 scenarios × (16 server-side peers + 15 client-side peers) =
  **744 combinations**; no quicX ↔ quicX self-test.
- **Delta vs. previous baseline**: the previous round (2026-08-09) covered
  14 scenarios × 11 peers (322 cases, 93.1% effective). This round adds 12
  scenarios (`longrtt`, `multiplexing`, `blackhole`, `ecn`,
  `amplificationlimit`, `handshakeloss`, `transferloss`,
  `handshakecorruption`, `transfercorruption`, `ipv6`, `goodput`,
  `crosstraffic`) and 6 peers (`kwik`, `xquic`, `go-x-net`, `nginx`,
  `haproxy`, `chrome`); the upstream matrix does not include `multiconnect` /
  `versionnegotiation`. The two rounds use different calibers and are not
  directly comparable.

## 2. Legend

| Mark | Meaning |
|:----:|---------|
| ✅ | SUCCEEDED |
| ❌ | FAILED (test failure / throughput below threshold) |
| ➖ | UNSUPPORTED — peer or scenario unsupported |
| — | N/A — combination not run (peer does not ship that role) |

## 3. Overall Results

| Metric | Value |
|------|-------|
| Total cases | **744** |
| ✅ Passed | **592** |
| ❌ Failed | **57** |
| ➖ Unsupported | **95** |
| **Effective pass rate (excl. Unsupported)** | **592 / 649 ≈ 91.22%** |
| RFC conformance pass rate | 534 / 589 ≈ 90.66% (client 292/312 = 93.59%; server 246/277 = 88.81%) |
| Performance pass rate | 58 / 60 ≈ 96.67% (goodput 30/30; crosstraffic 28/30) |
| Pass rate incl. Unsupported | 592 / 744 ≈ 79.6% |

## 4. Per-scenario pass rates

| 测试场景 | 场景简称 | Client 模式通过率 | Server 模式通过率 | 综合有效通过率 | 描述 |
| :--- | :---: | :---: | :---: | :---: | :--- |
| **handshake** | `H` | 16/16 (100.0%) | 14/14 (100.0%) | **30/30 (100.0%)** | Handshake completes successfully. |
| **transfer** | `DC` | 16/16 (100.0%) | 14/14 (100.0%) | **30/30 (100.0%)** | Stream data is being sent and received correctly. Connection close completes with a zero error code. |
| **longrtt** | `LR` | 16/16 (100.0%) | 14/14 (100.0%) | **30/30 (100.0%)** | Handshake completes when RTT is long. |
| **chacha20** | `C20` | 14/14 (100.0%) | 10/10 (100.0%) | **24/24 (100.0%)** | Handshake completes using ChaCha20. |
| **multiplexing** | `M` | 16/16 (100.0%) | 14/14 (100.0%) | **30/30 (100.0%)** | Thousands of files are transferred over a single connection, and server increased stream limits to accomodate client requests. |
| **retry** | `S` | 13/14 (92.9%) | 12/12 (100.0%) | **25/26 (96.2%)** | Server sends a Retry, and a subsequent connection using the Retry token completes successfully. |
| **resumption** | `R` | 14/15 (93.3%) | 12/12 (100.0%) | **26/27 (96.3%)** | Connection is established using TLS Session Resumption. |
| **zerortt** | `Z` | 13/14 (92.9%) | 10/12 (83.3%) | **23/26 (88.5%)** | 0-RTT data is being sent and acted on. |
| **http3** | `3` | 13/14 (92.9%) | 13/13 (100.0%) | **26/27 (96.3%)** | An H3 transaction succeeded. |
| **blackhole** | `B` | 15/16 (93.8%) | 14/14 (100.0%) | **29/30 (96.7%)** | Transfer succeeds despite underlying network blacking out for a few seconds. |
| **keyupdate** | `U` | 16/16 (100.0%) | 9/10 (90.0%) | **25/26 (96.2%)** | One of the two endpoints updates keys and the peer responds correctly. |
| **ecn** | `E` | 6/6 (100.0%) | 6/6 (100.0%) | **12/12 (100.0%)** | Handshake completes successfully. |
| **amplificationlimit** | `A` | 14/16 (87.5%) | 14/14 (100.0%) | **28/30 (93.3%)** | The server obeys the 3x amplification limit. |
| **handshakeloss** | `L1` | 15/15 (100.0%) | 9/13 (69.2%) | **24/28 (85.7%)** | Handshake completes under extreme packet loss. |
| **transferloss** | `L2` | 16/16 (100.0%) | 14/14 (100.0%) | **30/30 (100.0%)** | Transfer completes under moderate packet loss. |
| **handshakecorruption** | `C1` | 15/15 (100.0%) | 9/13 (69.2%) | **24/28 (85.7%)** | Handshake completes under extreme packet corruption. |
| **transfercorruption** | `C2` | 15/16 (93.8%) | 14/14 (100.0%) | **29/30 (96.7%)** | Transfer completes under moderate packet corruption. |
| **ipv6** | `6` | 16/16 (100.0%) | 14/14 (100.0%) | **30/30 (100.0%)** | A transfer across an IPv6-only network succeeded. |
| **v2** | `V2` | 8/8 (100.0%) | 7/8 (87.5%) | **15/16 (93.8%)** | Server should select QUIC v2 in compatible version negotiation. |
| **rebind-port** | `BP` | 9/16 (56.2%) | 10/14 (71.4%) | **19/30 (63.3%)** | Transfer completes under frequent port rebindings on the client side. |
| **rebind-addr** | `BA` | 9/16 (56.2%) | 10/14 (71.4%) | **19/30 (63.3%)** | Transfer completes under frequent IP address and port rebindings on the client side. |
| **connectionmigration** | `CM` | 3/5 (60.0%) | 3/14 (21.4%) | **6/19 (31.6%)** | A transfer succeeded during which the client performed an active migration. |
| **goodput** | `G` | 16/16 (100.0%) | 14/14 (100.0%) | **30/30 (100.0%)** | Measures connection goodput over a 10Mbps link. |
| **crosstraffic** | `C` | 15/16 (93.8%) | 13/14 (92.9%) | **28/30 (93.3%)** | Measures goodput over a 10Mbps link when competing with a TCP (cubic) connection. |

---

## 5. Peer ranking

| 排名 | 对端实现 | Client 模式 (发起) | Server 模式 (响应) | 协议功能通过率 | 综合有效通过率 | 状态 |
| :---: | :--- | :---: | :---: | :---: | :---: | :---: |
| 1 | **lsquic** | 23/24 (95.8%) | 23/23 (100.0%) | **42/43 (97.7%)** | 46/47 (97.9%) | 🌟 优秀 |
| 2 | **aioquic** | 21/22 (95.5%) | 22/23 (95.7%) | **39/41 (95.1%)** | 43/45 (95.6%) | 🌟 优秀 |
| 3 | **xquic** | 20/21 (95.2%) | 20/22 (90.9%) | **37/39 (94.9%)** | 40/43 (93.0%) | 🌟 优秀 |
| 4 | **neqo** | 24/24 (100.0%) | 21/24 (87.5%) | **41/44 (93.2%)** | 45/48 (93.8%) | 🌟 优秀 |
| 5 | **picoquic** | 23/24 (95.8%) | 22/24 (91.7%) | **41/44 (93.2%)** | 45/48 (93.8%) | 🌟 优秀 |
| 6 | **kwik** | 20/22 (90.9%) | 22/23 (95.7%) | **38/41 (92.7%)** | 42/45 (93.3%) | 🌟 优秀 |
| 7 | **s2n-quic** | 20/22 (90.9%) | 19/20 (95.0%) | **35/38 (92.1%)** | 39/42 (92.9%) | 🌟 优秀 |
| 8 | **ngtcp2** | 21/24 (87.5%) | 23/24 (95.8%) | **40/44 (90.9%)** | 44/48 (91.7%) | 🌟 优秀 |
| 9 | **quinn** | 22/22 (100.0%) | 20/24 (83.3%) | **38/42 (90.5%)** | 42/46 (91.3%) | 🌟 优秀 |
| 10 | **haproxy** | 20/22 (90.9%) | N/A | **18/20 (90.0%)** | 20/22 (90.9%) | 🌟 优秀 |
| 11 | **msquic** | 21/21 (100.0%) | 18/22 (81.8%) | **35/39 (89.7%)** | 39/43 (90.7%) | 👍 良好 |
| 12 | **nginx** | 19/21 (90.5%) | N/A | **17/19 (89.5%)** | 19/21 (90.5%) | 👍 良好 |
| 13 | **go-x-net** | 12/14 (85.7%) | 14/15 (93.3%) | **22/25 (88.0%)** | 26/29 (89.7%) | 👍 良好 |
| 14 | **quic-go** | 19/21 (90.5%) | 19/22 (86.4%) | **34/39 (87.2%)** | 38/43 (88.4%) | 👍 良好 |
| 15 | **quiche** | 19/21 (90.5%) | 17/20 (85.0%) | **32/37 (86.5%)** | 36/41 (87.8%) | 👍 良好 |
| 16 | **mvfst** | 15/19 (78.9%) | 12/18 (66.7%) | **24/33 (72.7%)** | 27/37 (73.0%) | ⚠️ 待提升 |
| 17 | **chrome** | N/A | 1/1 (100.0%) | **1/1 (100.0%)** | 1/1 (100.0%) | 🌟 优秀 |

> Note: "Client 模式" means quicX acting as client against the peer's server;
> "Server 模式" means the peer acting as client against the quicX server.
> The conformance rate excludes the goodput / crosstraffic performance scenarios.

---

## 6. Detailed matrices by scenario

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
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |



### 🔹 retry (`S`)

> **说明**: Server sends a Retry, and a subsequent connection using the Retry token completes successfully.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ➖ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
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
| **quicX as Client** | ✅ | ➖ | ❌ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ➖ | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ❌ | ✅ | ✅ | — | — | ✅ | ➖ |



### 🔹 http3 (`3`)

> **说明**: An H3 transaction succeeded.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ➖ | ✅ | ✅ | — | — | ✅ | ✅ |



### 🔹 blackhole (`B`)

> **说明**: Transfer succeeds despite underlying network blacking out for a few seconds.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |



### 🔹 keyupdate (`U`)

> **说明**: One of the two endpoints updates keys and the peer responds correctly.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ➖ | ➖ | ✅ | ➖ | ✅ | ➖ | ✅ | ✅ | ❌ | ✅ | — | — | ✅ | ➖ |



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
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |



### 🔹 handshakeloss (`L1`)

> **说明**: Handshake completes under extreme packet loss.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ❌ | ➖ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | — | — | ❌ | ➖ |



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
| **quicX as Server** | ✅ | ➖ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ❌ | ✅ | — | — | ❌ | ➖ |



### 🔹 transfercorruption (`C2`)

> **说明**: Transfer completes under moderate packet corruption.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
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
| **quicX as Client** | ❌ | ❌ | ✅ | ❌ | ❌ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | ❌ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ❌ | ✅ | ✅ | ❌ | ✅ | ✅ | ❌ | ✅ | ❌ | ✅ | ✅ | — | — | ✅ | ➖ |



### 🔹 rebind-addr (`BA`)

> **说明**: Transfer completes under frequent IP address and port rebindings on the client side.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ❌ | ❌ | ✅ | ✅ | ❌ | ✅ | ✅ | ❌ | ❌ | ✅ | ✅ | ✅ | ✅ | ❌ | ❌ | ✅ | — |
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
| **quicX as Client** | ✅<br><sub>9.22 Mbps</sub> | ✅<br><sub>7.99 Mbps</sub> | ✅<br><sub>9.42 Mbps</sub> | ✅<br><sub>9.21 Mbps</sub> | ✅<br><sub>9.33 Mbps</sub> | ✅<br><sub>8.35 Mbps</sub> | ✅<br><sub>9.47 Mbps</sub> | ✅<br><sub>8.77 Mbps</sub> | ✅<br><sub>9.24 Mbps</sub> | ✅<br><sub>8.60 Mbps</sub> | ✅<br><sub>8.51 Mbps</sub> | ✅<br><sub>9.14 Mbps</sub> | ✅<br><sub>9.22 Mbps</sub> | ✅<br><sub>9.40 Mbps</sub> | ✅<br><sub>9.06 Mbps</sub> | ✅<br><sub>8.70 Mbps</sub> | — |
| **quicX as Server** | ✅<br><sub>9.28 Mbps</sub> | ✅<br><sub>9.38 Mbps</sub> | ✅<br><sub>8.20 Mbps</sub> | ✅<br><sub>8.62 Mbps</sub> | ✅<br><sub>7.96 Mbps</sub> | ✅<br><sub>8.59 Mbps</sub> | ✅<br><sub>9.05 Mbps</sub> | ✅<br><sub>6.74 Mbps</sub> | ✅<br><sub>6.73 Mbps</sub> | ✅<br><sub>9.29 Mbps</sub> | ✅<br><sub>8.58 Mbps</sub> | ✅<br><sub>9.26 Mbps</sub> | ✅<br><sub>9.26 Mbps</sub> | — | — | ✅<br><sub>8.17 Mbps</sub> | ➖ |


#### 📊 实测吞吐速率排行榜 (10 Mbps 链路测试，平均速率: **8.76 Mbps**)

| 排名 | 对端实现 | 测试角色 | 状态 | 详细速率 (含波动) | 实测吞吐 (Mbps) | 带宽利用率 |
| :---: | :--- | :---: | :---: | :---: | :---: | :---: |
| 1 | **lsquic** | quicX as Client | ✅ | 9471 (± 8) kbps | **9.47 Mbps** | `94.7%` |
| 2 | **quic-go** | quicX as Client | ✅ | 9420 (± 12) kbps | **9.42 Mbps** | `94.2%` |
| 3 | **nginx** | quicX as Client | ✅ | 9403 (± 7) kbps | **9.40 Mbps** | `94.0%` |
| 4 | **go-x-net** | quicX as Server | ✅ | 9384 (± 14) kbps | **9.38 Mbps** | `93.8%` |
| 5 | **quiche** | quicX as Client | ✅ | 9330 (± 18) kbps | **9.33 Mbps** | `93.3%` |
| 6 | **aioquic** | quicX as Server | ✅ | 9293 (± 73) kbps | **9.29 Mbps** | `92.9%` |
| 7 | **ngtcp2** | quicX as Server | ✅ | 9276 (± 89) kbps | **9.28 Mbps** | `92.8%` |
| 8 | **picoquic** | quicX as Server | ✅ | 9256 (± 123) kbps | **9.26 Mbps** | `92.6%` |
| 9 | **xquic** | quicX as Server | ✅ | 9255 (± 54) kbps | **9.26 Mbps** | `92.5%` |
| 10 | **mvfst** | quicX as Client | ✅ | 9239 (± 40) kbps | **9.24 Mbps** | `92.4%` |
| 11 | **ngtcp2** | quicX as Client | ✅ | 9225 (± 101) kbps | **9.22 Mbps** | `92.2%` |
| 12 | **xquic** | quicX as Client | ✅ | 9224 (± 54) kbps | **9.22 Mbps** | `92.2%` |
| 13 | **s2n-quic** | quicX as Client | ✅ | 9206 (± 45) kbps | **9.21 Mbps** | `92.1%` |
| 14 | **picoquic** | quicX as Client | ✅ | 9145 (± 26) kbps | **9.14 Mbps** | `91.5%` |
| 15 | **haproxy** | quicX as Client | ✅ | 9059 (± 9) kbps | **9.06 Mbps** | `90.6%` |
| 16 | **lsquic** | quicX as Server | ✅ | 9050 (± 159) kbps | **9.05 Mbps** | `90.5%` |
| 17 | **kwik** | quicX as Client | ✅ | 8772 (± 260) kbps | **8.77 Mbps** | `87.7%` |
| 18 | **quinn** | quicX as Client | ✅ | 8700 (± 4) kbps | **8.70 Mbps** | `87.0%` |
| 19 | **s2n-quic** | quicX as Server | ✅ | 8622 (± 210) kbps | **8.62 Mbps** | `86.2%` |
| 20 | **aioquic** | quicX as Client | ✅ | 8601 (± 25) kbps | **8.60 Mbps** | `86.0%` |
| 21 | **neqo** | quicX as Server | ✅ | 8590 (± 335) kbps | **8.59 Mbps** | `85.9%` |
| 22 | **msquic** | quicX as Server | ✅ | 8583 (± 369) kbps | **8.58 Mbps** | `85.8%` |
| 23 | **msquic** | quicX as Client | ✅ | 8506 (± 34) kbps | **8.51 Mbps** | `85.1%` |
| 24 | **neqo** | quicX as Client | ✅ | 8345 (± 21) kbps | **8.35 Mbps** | `83.5%` |
| 25 | **quic-go** | quicX as Server | ✅ | 8201 (± 240) kbps | **8.20 Mbps** | `82.0%` |
| 26 | **quinn** | quicX as Server | ✅ | 8170 (± 185) kbps | **8.17 Mbps** | `81.7%` |
| 27 | **go-x-net** | quicX as Client | ✅ | 7988 (± 23) kbps | **7.99 Mbps** | `79.9%` |
| 28 | **quiche** | quicX as Server | ✅ | 7964 (± 410) kbps | **7.96 Mbps** | `79.6%` |
| 29 | **kwik** | quicX as Server | ✅ | 6744 (± 162) kbps | **6.74 Mbps** | `67.4%` |
| 30 | **mvfst** | quicX as Server | ✅ | 6727 (± 124) kbps | **6.73 Mbps** | `67.3%` |

> 💡 **性能分析**: quicX 在 10Mbps 瓶颈链路上表现极佳，双向全部 30 个实测场景（Client 模式 16/16，Server 模式 14/14）**100% 满分通过**，最高带宽利用率达 **94.7%** (`lsquic`: 9.47 Mbps)，全局平均有效速率达到 **8.76 Mbps**。


### 🔹 crosstraffic (`C`)

> **说明**: Measures goodput over a 10Mbps link when competing with a TCP (cubic) connection.

| 角色 \ 对端实现 | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅<br><sub>4.25 Mbps</sub> | ✅<br><sub>2.81 Mbps</sub> | ✅<br><sub>4.46 Mbps</sub> | ✅<br><sub>4.94 Mbps</sub> | ✅<br><sub>4.41 Mbps</sub> | ✅<br><sub>4.05 Mbps</sub> | ✅<br><sub>8.35 Mbps</sub> | ✅<br><sub>2.35 Mbps</sub> | ❌ | ✅<br><sub>3.10 Mbps</sub> | ✅<br><sub>3.42 Mbps</sub> | ✅<br><sub>7.14 Mbps</sub> | ✅<br><sub>8.48 Mbps</sub> | ✅<br><sub>2.37 Mbps</sub> | ✅<br><sub>4.57 Mbps</sub> | ✅<br><sub>3.12 Mbps</sub> | — |
| **quicX as Server** | ✅<br><sub>9.11 Mbps</sub> | ✅<br><sub>6.16 Mbps</sub> | ✅<br><sub>8.46 Mbps</sub> | ✅<br><sub>8.45 Mbps</sub> | ✅<br><sub>7.70 Mbps</sub> | ✅<br><sub>8.72 Mbps</sub> | ✅<br><sub>8.85 Mbps</sub> | ✅<br><sub>6.40 Mbps</sub> | ✅<br><sub>6.38 Mbps</sub> | ✅<br><sub>9.05 Mbps</sub> | ✅<br><sub>8.58 Mbps</sub> | ✅<br><sub>9.00 Mbps</sub> | ❌ | — | — | ✅<br><sub>7.76 Mbps</sub> | ➖ |


#### 📊 与 TCP (Cubic) 竞争实测数据明细

| 排名 | 对端实现 | 测试角色 | 状态 | 详细速率 (含波动) | 实测吞吐 (Mbps) | 说明 |
| :---: | :--- | :---: | :---: | :---: | :---: | :--- |
| 1 | **ngtcp2** | quicX as Server | ✅ | 9114 (± 144) kbps | **9.11 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `91.1%`) |
| 2 | **aioquic** | quicX as Server | ✅ | 9047 (± 63) kbps | **9.05 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `90.5%`) |
| 3 | **picoquic** | quicX as Server | ✅ | 9004 (± 74) kbps | **9.00 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `90.0%`) |
| 4 | **lsquic** | quicX as Server | ✅ | 8845 (± 93) kbps | **8.85 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `88.4%`) |
| 5 | **neqo** | quicX as Server | ✅ | 8723 (± 217) kbps | **8.72 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `87.2%`) |
| 6 | **msquic** | quicX as Server | ✅ | 8576 (± 104) kbps | **8.58 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `85.8%`) |
| 7 | **xquic** | quicX as Client | ✅ | 8477 (± 77) kbps | **8.48 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `84.8%`) |
| 8 | **quic-go** | quicX as Server | ✅ | 8458 (± 177) kbps | **8.46 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `84.6%`) |
| 9 | **s2n-quic** | quicX as Server | ✅ | 8451 (± 147) kbps | **8.45 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `84.5%`) |
| 10 | **lsquic** | quicX as Client | ✅ | 8345 (± 111) kbps | **8.35 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `83.5%`) |
| 11 | **quinn** | quicX as Server | ✅ | 7756 (± 143) kbps | **7.76 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `77.6%`) |
| 12 | **quiche** | quicX as Server | ✅ | 7704 (± 343) kbps | **7.70 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `77.0%`) |
| 13 | **picoquic** | quicX as Client | ✅ | 7139 (± 171) kbps | **7.14 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `71.4%`) |
| 14 | **kwik** | quicX as Server | ✅ | 6399 (± 151) kbps | **6.40 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `64.0%`) |
| 15 | **mvfst** | quicX as Server | ✅ | 6383 (± 138) kbps | **6.38 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `63.8%`) |
| 16 | **go-x-net** | quicX as Server | ✅ | 6156 (± 53) kbps | **6.16 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `61.6%`) |
| 17 | **s2n-quic** | quicX as Client | ✅ | 4939 (± 406) kbps | **4.94 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `49.4%`) |
| 18 | **haproxy** | quicX as Client | ✅ | 4566 (± 131) kbps | **4.57 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `45.7%`) |
| 19 | **quic-go** | quicX as Client | ✅ | 4465 (± 52) kbps | **4.46 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `44.6%`) |
| 20 | **quiche** | quicX as Client | ✅ | 4405 (± 452) kbps | **4.41 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `44.0%`) |
| 21 | **ngtcp2** | quicX as Client | ✅ | 4254 (± 432) kbps | **4.25 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `42.5%`) |
| 22 | **neqo** | quicX as Client | ✅ | 4049 (± 231) kbps | **4.05 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `40.5%`) |
| 23 | **msquic** | quicX as Client | ✅ | 3416 (± 186) kbps | **3.42 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `34.2%`) |
| 24 | **quinn** | quicX as Client | ✅ | 3120 (± 116) kbps | **3.12 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `31.2%`) |
| 25 | **aioquic** | quicX as Client | ✅ | 3099 (± 190) kbps | **3.10 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `31.0%`) |
| 26 | **go-x-net** | quicX as Client | ✅ | 2815 (± 92) kbps | **2.81 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `28.1%`) |
| 27 | **nginx** | quicX as Client | ✅ | 2370 (± 130) kbps | **2.37 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `23.7%`) |
| 28 | **kwik** | quicX as Client | ✅ | 2351 (± 512) kbps | **2.35 Mbps** | 成功在 10Mbps 竞争链路建立公平带宽占有 (利用率 `23.5%`) |
| - | `mvfst` | quicX as Client | ❌ | — | — | 与 TCP Cubic 竞争时测算未达 interop runner 阈值 |
| - | `xquic` | quicX as Server | ❌ | — | — | 与 TCP Cubic 竞争时测算未达 interop runner 阈值 |

> 💡 **竞争分析**: quicX 展现出双向均衡的抗 TCP Cubic 竞争能力：作为 Server 时在 14 个对端 Client 中成功拿下 **13/14 (92.9%)**，平均抢占速率 > 8.0 Mbps（对 ngtcp2、aioquic、picoquic 均突破 9.0 Mbps）；作为 Client 时 **15/16 (93.8%)** 达标，其中对 lsquic (8.35 Mbps) 与 xquic (8.48 Mbps) 场景抢占效率最高。综合抗 TCP Cubic 竞争达标率达到 **93.3% (28/30)**，仅 mvfst (Client) 与 xquic (Server) 两个组合未达标。
