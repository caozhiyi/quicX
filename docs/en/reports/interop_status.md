# quicX Interoperability Status

This document records the **latest baseline interoperability results** of

quicX against the major QUIC implementations and is the canonical version

to cite externally.

For test methodology, commands, and environment, see

[`guide/interop_runbook.md`](../guide/interop_runbook.md).

| Item | Value |
|---|---|
| **Run mode** | upstream quic-interop-runner full matrix (quic-network-simulator / ns-3 topology) |
| **Peers under test** | 17 third-party implementations (14 bidirectional + nginx / haproxy server-only + chrome client-only) |
| **Scenarios under test** | 24 (22 protocol-conformance + 2 performance measurements) |
| **Total cases** | 744 (PASS 588 / FAIL 61 / UNSUPPORTED 95) |
| **Overall effective pass rate** | **90.60%** (588 / 649, excluding UNSUPPORTED) |
| **RFC conformance pass rate** | **90.15%** (531 / 589; client mode 292/312 = 93.59%, server mode 239/277 = 86.28%) |
| **Performance pass rate** | **95.00%** (57 / 60) |

---

## TL;DR

- ✅ Across the full 24-scenario × 17-peer matrix (744 cases), quicX achieves an
  **overall effective pass rate of 90.60%**.
- ✅ **12 scenarios at 100%**: `handshake`, `transfer`, `longrtt`, `chacha20`,
  `retry`, `http3`, `blackhole`, `ecn`, `transferloss`, `transfercorruption`,
  `ipv6`, plus `goodput` (30/30, avg 8.67 Mbps,
  peak bandwidth utilization 94.7%).
- ✅ `crosstraffic` (vs TCP Cubic) 27/30 (90.0%), balanced in both directions;
  only kwik / mvfst (client) and xquic (server) miss the threshold.
- ✅ Versus the previous round (91.22%):
  `quicx → mvfst` `retry`, `quicx → quic-go` `http3`, `quicx → picoquic`
  `blackhole`, and `quicx → mvfst` `transfercorruption` recovered to PASS;
  client-mode `zerortt` and server-mode `handshakeloss` / `handshakecorruption`
  improved markedly.
- ⚠️ Current weak spots: `connectionmigration` (31.6%),
  `rebind-addr` (60.0%), `rebind-port` / `amplificationlimit` (66.7% each —
  amplificationlimit server mode fell to 6/14, the main regression of this
  round), and server-mode `keyupdate` (8/10).

---

## 1. Overview

- **Source**: one full-matrix run of the upstream
  [quic-interop-runner](https://github.com/quic-interop/quic-interop-runner).
- **Environment**: Linux host, Docker + ns-3 network simulation
  (leftnet ↔ sim ↔ rightnet); quicX runs from locally built
  `interop_{server,client}` binaries.
- **Coverage**: 24 scenarios × (16 server-side peers + 15 client-side peers) =
  **744 combinations**; no quicX ↔ quicX self-test.
- **Delta vs. previous baseline**: the previous round used the
  same 24-scenario × 17-peer caliber (744 cases, 91.22% effective), so the two
  rounds are directly comparable. The overall rate dropped 0.62pp, driven
  mainly by server-mode `amplificationlimit` (14/14 → 6/14), `keyupdate`
  (9/10 → 8/10), and newly-failing `xquic` server `multiplexing`; meanwhile
  `retry` (25/26 → 26/26), `http3` (26/27 → 27/27), `blackhole`
  (29/30 → 30/30), and `transfercorruption` (29/30 → 30/30) recovered to full
  marks, and `zerortt` (23/26 → 24/26) and `handshakecorruption`
  (24/28 → 26/28) improved. The upstream matrix does not include
  `multiconnect` / `versionnegotiation`.

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
| ✅ Passed | **588** |
| ❌ Failed | **61** |
| ➖ Unsupported | **95** |
| **Effective pass rate (excl. Unsupported)** | **588 / 649 ≈ 90.60%** |
| RFC conformance pass rate | 531 / 589 ≈ 90.15% (client 292/312 = 93.59%; server 239/277 = 86.28%) |
| Performance pass rate | 57 / 60 ≈ 95.00% (goodput 30/30; crosstraffic 27/30) |
| Pass rate incl. Unsupported | 588 / 744 ≈ 79.0% |

## 4. Per-scenario pass rates

| Test scenario | Short name | Client-mode pass rate | Server-mode pass rate | Combined effective pass rate | Description |
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
| **rebind-addr** | `BA` | 9/16 (56.2%) | 10/14 (71.4%) | **19/30 (63.3%)** | Transfer completes under frequent IP address and port rebindings on the client side. |
| **connectionmigration** | `CM` | 3/5 (60.0%) | 3/14 (21.4%) | **6/19 (31.6%)** | A transfer succeeded during which the client performed an active migration. |
| **goodput** | `G` | 16/16 (100.0%) | 14/14 (100.0%) | **30/30 (100.0%)** | Measures connection goodput over a 10Mbps link. |
| **crosstraffic** | `C` | 14/16 (87.5%) | 13/14 (92.9%) | **27/30 (90.0%)** | Measures goodput over a 10Mbps link when competing with a TCP (cubic) connection. |

---

## 5. Peer ranking

| Rank | Implementation | Client mode (initiator) | Server mode (responder) | Protocol-feature pass rate | Combined effective pass rate | Status |
| :---: | :--- | :---: | :---: | :---: | :---: | :---: |
| 1 | **aioquic** | 22/22 (100.0%) | 22/23 (95.7%) | **40/41 (97.6%)** | 44/45 (97.8%) | 🌟 Excellent |
| 2 | **picoquic** | 24/24 (100.0%) | 22/24 (91.7%) | **42/44 (95.5%)** | 46/48 (95.8%) | 🌟 Excellent |
| 3 | **lsquic** | 23/24 (95.8%) | 22/23 (95.7%) | **41/43 (95.3%)** | 45/47 (95.7%) | 🌟 Excellent |
| 4 | **ngtcp2** | 21/24 (87.5%) | 24/24 (100.0%) | **41/44 (93.2%)** | 45/48 (93.8%) | 🌟 Excellent |
| 5 | **kwik** | 19/22 (86.4%) | 22/23 (95.7%) | **38/41 (92.7%)** | 41/45 (91.1%) | 🌟 Excellent |
| 6 | **neqo** | 24/24 (100.0%) | 20/24 (83.3%) | **40/44 (90.9%)** | 44/48 (91.7%) | 🌟 Excellent |
| 7 | **quinn** | 22/22 (100.0%) | 20/24 (83.3%) | **38/42 (90.5%)** | 42/46 (91.3%) | 🌟 Excellent |
| 8 | **haproxy** | 20/22 (90.9%) | N/A | **18/20 (90.0%)** | 20/22 (90.9%) | 🌟 Excellent |
| 9 | **quic-go** | 21/21 (100.0%) | 18/22 (81.8%) | **35/39 (89.7%)** | 39/43 (90.7%) | 👍 Good |
| 10 | **msquic** | 21/21 (100.0%) | 18/22 (81.8%) | **35/39 (89.7%)** | 39/43 (90.7%) | 👍 Good |
| 11 | **s2n-quic** | 20/22 (90.9%) | 18/20 (90.0%) | **34/38 (89.5%)** | 38/42 (90.5%) | 👍 Good |
| 12 | **nginx** | 19/21 (90.5%) | N/A | **17/19 (89.5%)** | 19/21 (90.5%) | 👍 Good |
| 13 | **quiche** | 19/21 (90.5%) | 18/20 (90.0%) | **33/37 (89.2%)** | 37/41 (90.2%) | 👍 Good |
| 14 | **xquic** | 19/21 (90.5%) | 17/22 (77.3%) | **33/39 (84.6%)** | 36/43 (83.7%) | 👍 Good |
| 15 | **go-x-net** | 12/14 (85.7%) | 13/15 (86.7%) | **21/25 (84.0%)** | 25/29 (86.2%) | 👍 Good |
| 16 | **mvfst** | 16/19 (84.2%) | 11/18 (61.1%) | **24/33 (72.7%)** | 27/37 (73.0%) | ⚠️ Needs improvement |
| 17 | **chrome** | N/A | 1/1 (100.0%) | **1/1 (100.0%)** | 1/1 (100.0%) | 🌟 Excellent |

> Note: "Client mode" means quicX acting as client against the peer's server;
> "Server mode" means the peer acting as client against the quicX server.
> The conformance rate excludes the goodput / crosstraffic performance scenarios.

---

## 6. Detailed matrices by scenario

## 📋 Detailed per-scenario matrix tables

### 🔹 handshake (`H`)

> **Note**: Handshake completes successfully.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |



### 🔹 transfer (`DC`)

> **Note**: Stream data is being sent and received correctly. Connection close completes with a zero error code.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |



### 🔹 longrtt (`LR`)

> **Note**: Handshake completes when RTT is long.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |



### 🔹 chacha20 (`C20`)

> **Note**: Handshake completes using ChaCha20.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ➖ | ✅ | ➖ | ➖ | ✅ | ✅ | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |



### 🔹 multiplexing (`M`)

> **Note**: Thousands of files are transferred over a single connection, and server increased stream limits to accomodate client requests.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | — | — | ✅ | ➖ |



### 🔹 retry (`S`)

> **Note**: Server sends a Retry, and a subsequent connection using the Retry token completes successfully.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 resumption (`R`)

> **Note**: Connection is established using TLS Session Resumption.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |



### 🔹 zerortt (`Z`)

> **Note**: 0-RTT data is being sent and acted on.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ➖ | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ❌ | ✅ | ✅ | — | — | ✅ | ➖ |



### 🔹 http3 (`3`)

> **Note**: An H3 transaction succeeded.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ➖ | ✅ | ✅ | — | — | ✅ | ✅ |



### 🔹 blackhole (`B`)

> **Note**: Transfer succeeds despite underlying network blacking out for a few seconds.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |



### 🔹 keyupdate (`U`)

> **Note**: One of the two endpoints updates keys and the peer responds correctly.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ➖ | ➖ | ✅ | ➖ | ✅ | ➖ | ✅ | ✅ | ❌ | ✅ | — | — | ❌ | ➖ |



### 🔹 ecn (`E`)

> **Note**: Handshake completes successfully.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ➖ | ✅ | ➖ | ✅ | ✅ | ➖ | ➖ | ➖ | ➖ | ✅ | ➖ | ➖ | ➖ | ✅ | — |
| **quicX as Server** | ✅ | ➖ | ➖ | ✅ | ➖ | ✅ | ✅ | ➖ | ➖ | ➖ | ➖ | ✅ | ➖ | — | — | ✅ | ➖ |



### 🔹 amplificationlimit (`A`)

> **Note**: The server obeys the 3x amplification limit.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ❌ | ❌ | ❌ | ✅ | ❌ | ❌ | ✅ | ❌ | ✅ | ✅ | ❌ | ✅ | — | — | ❌ | ➖ |



### 🔹 handshakeloss (`L1`)

> **Note**: Handshake completes under extreme packet loss.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ➖ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ❌ | — | — | ✅ | ➖ |



### 🔹 transferloss (`L2`)

> **Note**: Transfer completes under moderate packet loss.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |



### 🔹 handshakecorruption (`C1`)

> **Note**: Handshake completes under extreme packet corruption.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ❌ | — | — | ✅ | ➖ |



### 🔹 transfercorruption (`C2`)

> **Note**: Transfer completes under moderate packet corruption.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |



### 🔹 ipv6 (`6`)

> **Note**: A transfer across an IPv6-only network succeeded.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |



### 🔹 v2 (`V2`)

> **Note**: Server should select QUIC v2 in compatible version negotiation.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ➖ | ➖ | ➖ | ✅ | ✅ | ✅ | ➖ | ✅ | ✅ | ✅ | ➖ | ➖ | ✅ | ➖ | — |
| **quicX as Server** | ✅ | ➖ | ➖ | ➖ | ➖ | ✅ | ✅ | ✅ | ➖ | ✅ | ✅ | ✅ | ➖ | — | — | ❌ | ➖ |



### 🔹 rebind-port (`BP`)

> **Note**: Transfer completes under frequent port rebindings on the client side.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ❌ | ❌ | ✅ | ✅ | ❌ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | ❌ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ❌ | ✅ | ✅ | ❌ | ✅ | ✅ | ❌ | ✅ | ❌ | ✅ | ✅ | — | — | ✅ | ➖ |



### 🔹 rebind-addr (`BA`)

> **Note**: Transfer completes under frequent IP address and port rebindings on the client side.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ❌ | ❌ | ✅ | ❌ | ❌ | ✅ | ✅ | ❌ | ❌ | ✅ | ✅ | ✅ | ✅ | ❌ | ❌ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ❌ | ✅ | ✅ | ❌ | ✅ | ✅ | ❌ | ✅ | ❌ | ✅ | ✅ | — | — | ✅ | ➖ |



### 🔹 connectionmigration (`CM`)

> **Note**: A transfer succeeded during which the client performed an active migration.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ❌ | ➖ | ➖ | ❌ | ➖ | ✅ | ✅ | ➖ | ➖ | ➖ | ➖ | ✅ | ➖ | ➖ | ➖ | ➖ | — |
| **quicX as Server** | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ | ✅ | ❌ | ❌ | ❌ | ❌ | ✅ | ❌ | — | — | ❌ | ➖ |



### 🔹 goodput (`G`)

> **Note**: Measures connection goodput over a 10Mbps link.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅<br><sub>9.30 Mbps</sub> | ✅<br><sub>7.97 Mbps</sub> | ✅<br><sub>9.43 Mbps</sub> | ✅<br><sub>9.19 Mbps</sub> | ✅<br><sub>9.35 Mbps</sub> | ✅<br><sub>8.35 Mbps</sub> | ✅<br><sub>9.47 Mbps</sub> | ✅<br><sub>7.97 Mbps</sub> | ✅<br><sub>9.26 Mbps</sub> | ✅<br><sub>8.55 Mbps</sub> | ✅<br><sub>8.54 Mbps</sub> | ✅<br><sub>9.17 Mbps</sub> | ✅<br><sub>9.26 Mbps</sub> | ✅<br><sub>9.40 Mbps</sub> | ✅<br><sub>8.97 Mbps</sub> | ✅<br><sub>8.69 Mbps</sub> | — |
| **quicX as Server** | ✅<br><sub>9.20 Mbps</sub> | ✅<br><sub>9.38 Mbps</sub> | ✅<br><sub>8.17 Mbps</sub> | ✅<br><sub>8.77 Mbps</sub> | ✅<br><sub>6.81 Mbps</sub> | ✅<br><sub>8.70 Mbps</sub> | ✅<br><sub>8.88 Mbps</sub> | ✅<br><sub>7.11 Mbps</sub> | ✅<br><sub>6.82 Mbps</sub> | ✅<br><sub>9.20 Mbps</sub> | ✅<br><sub>8.23 Mbps</sub> | ✅<br><sub>8.59 Mbps</sub> | ✅<br><sub>9.03 Mbps</sub> | — | — | ✅<br><sub>8.46 Mbps</sub> | ➖ |


#### 📊 Measured goodput leaderboard (10 Mbps link test, average: **8.67 Mbps**)

| Rank | Implementation | Role | Status | Rate detail (± jitter) | Measured goodput (Mbps) | Bandwidth utilization |
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

> 💡 **Performance analysis**: quicX performs excellently on the 10 Mbps bottleneck link — all 30 measured scenarios in both directions (Client mode 16/16, Server mode 14/14) pass with a **100% score**, the top bandwidth utilization reaches **94.7%** (`lsquic`: 9.47 Mbps), and the global average effective rate is **8.67 Mbps**.


### 🔹 crosstraffic (`C`)

> **Note**: Measures goodput over a 10Mbps link when competing with a TCP (cubic) connection.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅<br><sub>4.38 Mbps</sub> | ✅<br><sub>2.69 Mbps</sub> | ✅<br><sub>4.58 Mbps</sub> | ✅<br><sub>5.08 Mbps</sub> | ✅<br><sub>4.74 Mbps</sub> | ✅<br><sub>4.03 Mbps</sub> | ✅<br><sub>8.42 Mbps</sub> | ❌ | ❌ | ✅<br><sub>3.13 Mbps</sub> | ✅<br><sub>3.31 Mbps</sub> | ✅<br><sub>7.26 Mbps</sub> | ✅<br><sub>8.56 Mbps</sub> | ✅<br><sub>2.42 Mbps</sub> | ✅<br><sub>4.62 Mbps</sub> | ✅<br><sub>3.12 Mbps</sub> | — |
| **quicX as Server** | ✅<br><sub>9.06 Mbps</sub> | ✅<br><sub>6.11 Mbps</sub> | ✅<br><sub>8.41 Mbps</sub> | ✅<br><sub>8.62 Mbps</sub> | ✅<br><sub>6.71 Mbps</sub> | ✅<br><sub>8.77 Mbps</sub> | ✅<br><sub>8.77 Mbps</sub> | ✅<br><sub>6.58 Mbps</sub> | ✅<br><sub>6.64 Mbps</sub> | ✅<br><sub>8.96 Mbps</sub> | ✅<br><sub>8.55 Mbps</sub> | ✅<br><sub>8.50 Mbps</sub> | ❌ | — | — | ✅<br><sub>8.10 Mbps</sub> | ➖ |


#### 📊 Detailed results of the TCP (Cubic) competition runs

| Rank | Implementation | Role | Status | Rate detail (± jitter) | Measured goodput (Mbps) | Notes |
| :---: | :--- | :---: | :---: | :---: | :---: | :--- |
| 1 | **ngtcp2** | quicX as Server | ✅ | 9058 (± 129) kbps | **9.06 Mbps** | Fair bandwidth share established on the 10 Mbps competing link (utilization `90.6%`) |
| 2 | **aioquic** | quicX as Server | ✅ | 8960 (± 84) kbps | **8.96 Mbps** | Fair bandwidth share established on the 10 Mbps competing link (utilization `89.6%`) |
| 3 | **lsquic** | quicX as Server | ✅ | 8770 (± 168) kbps | **8.77 Mbps** | Fair bandwidth share established on the 10 Mbps competing link (utilization `87.7%`) |
| 4 | **neqo** | quicX as Server | ✅ | 8765 (± 268) kbps | **8.77 Mbps** | Fair bandwidth share established on the 10 Mbps competing link (utilization `87.6%`) |
| 5 | **s2n-quic** | quicX as Server | ✅ | 8621 (± 68) kbps | **8.62 Mbps** | Fair bandwidth share established on the 10 Mbps competing link (utilization `86.2%`) |
| 6 | **xquic** | quicX as Client | ✅ | 8564 (± 116) kbps | **8.56 Mbps** | Fair bandwidth share established on the 10 Mbps competing link (utilization `85.6%`) |
| 7 | **msquic** | quicX as Server | ✅ | 8548 (± 273) kbps | **8.55 Mbps** | Fair bandwidth share established on the 10 Mbps competing link (utilization `85.5%`) |
| 8 | **picoquic** | quicX as Server | ✅ | 8503 (± 137) kbps | **8.50 Mbps** | Fair bandwidth share established on the 10 Mbps competing link (utilization `85.0%`) |
| 9 | **lsquic** | quicX as Client | ✅ | 8423 (± 125) kbps | **8.42 Mbps** | Fair bandwidth share established on the 10 Mbps competing link (utilization `84.2%`) |
| 10 | **quic-go** | quicX as Server | ✅ | 8414 (± 97) kbps | **8.41 Mbps** | Fair bandwidth share established on the 10 Mbps competing link (utilization `84.1%`) |
| 11 | **quinn** | quicX as Server | ✅ | 8097 (± 226) kbps | **8.10 Mbps** | Fair bandwidth share established on the 10 Mbps competing link (utilization `81.0%`) |
| 12 | **picoquic** | quicX as Client | ✅ | 7256 (± 129) kbps | **7.26 Mbps** | Fair bandwidth share established on the 10 Mbps competing link (utilization `72.6%`) |
| 13 | **quiche** | quicX as Server | ✅ | 6706 (± 310) kbps | **6.71 Mbps** | Fair bandwidth share established on the 10 Mbps competing link (utilization `67.1%`) |
| 14 | **mvfst** | quicX as Server | ✅ | 6642 (± 163) kbps | **6.64 Mbps** | Fair bandwidth share established on the 10 Mbps competing link (utilization `66.4%`) |
| 15 | **kwik** | quicX as Server | ✅ | 6578 (± 108) kbps | **6.58 Mbps** | Fair bandwidth share established on the 10 Mbps competing link (utilization `65.8%`) |
| 16 | **go-x-net** | quicX as Server | ✅ | 6106 (± 107) kbps | **6.11 Mbps** | Fair bandwidth share established on the 10 Mbps competing link (utilization `61.1%`) |
| 17 | **s2n-quic** | quicX as Client | ✅ | 5076 (± 152) kbps | **5.08 Mbps** | Fair bandwidth share established on the 10 Mbps competing link (utilization `50.8%`) |
| 18 | **quiche** | quicX as Client | ✅ | 4737 (± 481) kbps | **4.74 Mbps** | Fair bandwidth share established on the 10 Mbps competing link (utilization `47.4%`) |
| 19 | **haproxy** | quicX as Client | ✅ | 4620 (± 77) kbps | **4.62 Mbps** | Fair bandwidth share established on the 10 Mbps competing link (utilization `46.2%`) |
| 20 | **quic-go** | quicX as Client | ✅ | 4582 (± 191) kbps | **4.58 Mbps** | Fair bandwidth share established on the 10 Mbps competing link (utilization `45.8%`) |
| 21 | **ngtcp2** | quicX as Client | ✅ | 4378 (± 746) kbps | **4.38 Mbps** | Fair bandwidth share established on the 10 Mbps competing link (utilization `43.8%`) |
| 22 | **neqo** | quicX as Client | ✅ | 4033 (± 87) kbps | **4.03 Mbps** | Fair bandwidth share established on the 10 Mbps competing link (utilization `40.3%`) |
| 23 | **msquic** | quicX as Client | ✅ | 3315 (± 70) kbps | **3.31 Mbps** | Fair bandwidth share established on the 10 Mbps competing link (utilization `33.1%`) |
| 24 | **aioquic** | quicX as Client | ✅ | 3133 (± 157) kbps | **3.13 Mbps** | Fair bandwidth share established on the 10 Mbps competing link (utilization `31.3%`) |
| 25 | **quinn** | quicX as Client | ✅ | 3125 (± 123) kbps | **3.12 Mbps** | Fair bandwidth share established on the 10 Mbps competing link (utilization `31.2%`) |
| 26 | **go-x-net** | quicX as Client | ✅ | 2694 (± 94) kbps | **2.69 Mbps** | Fair bandwidth share established on the 10 Mbps competing link (utilization `26.9%`) |
| 27 | **nginx** | quicX as Client | ✅ | 2416 (± 99) kbps | **2.42 Mbps** | Fair bandwidth share established on the 10 Mbps competing link (utilization `24.2%`) |
| - | `kwik`, `mvfst` | quicX as Client | ❌ | — | — | Below the interop-runner threshold when competing against TCP Cubic |
| - | `xquic` | quicX as Server | ❌ | — | — | Below the interop-runner threshold when competing against TCP Cubic |

> 💡 **Competition analysis**: quicX shows balanced, bidirectional resilience against TCP Cubic competition: as Server it secures **13/14 (92.9%)** of the 14 peer clients with an average captured rate > 8.0 Mbps (above 9.0 Mbps against ngtcp2, and 8.96 Mbps against aioquic); as Client **14/16 (87.5%)** pass, with the highest capture efficiency against xquic (8.56 Mbps) and lsquic (8.42 Mbps). The combined anti-TCP-Cubic pass rate reaches **90.0% (27/30)**; only the kwik / mvfst (Client) and xquic (Server) combinations fall short.
