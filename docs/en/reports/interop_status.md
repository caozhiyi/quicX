# quicX Interoperability Status

This document records the **latest baseline interoperability results** of quicX against the major QUIC implementations and is the canonical version to cite externally.

For test methodology, commands, and environment, see [`guide/interop_runbook.md`](../guide/interop_runbook.md).

| Item | Value |
|---|---|
| **Run mode** | upstream quic-interop-runner full matrix (quic-network-simulator / ns-3 topology) |
| **Peers under test** | 17 third-party implementations (14 bidirectional + nginx / haproxy server-only + chrome client-only) |
| **Scenarios under test** | 24 (22 protocol-conformance + 2 performance measurements) |
| **Total cases** | 768 (PASS 625 / FAIL 48 / UNSUPPORTED 95), including a quicX ↔ quicX self-check |
| **Overall effective pass rate** | **92.87%** (625 / 673, excluding UNSUPPORTED) |
| **RFC conformance pass rate** | **93.62%** (572 / 611; client mode 294/312 = 94.23%, server mode 278/299 = 92.98%) |
| **Performance pass rate** | **85.48%** (53 / 62) |

---

## TL;DR

- ✅ Across the full 24-scenario × 17-peer matrix plus a quicX ↔ quicX self-check (768 cases, run on 2026-10-10), quicX achieves an **overall effective pass rate of 92.87%**, up 2.27pp from the previous round (90.60%).
- ✅ **12 scenarios at 100%**: `handshake`, `transfer`, `longrtt`, `chacha20`, `retry`, `http3`, `blackhole`, `ecn`, `transferloss`, `transfercorruption`, `ipv6`, plus `goodput` (30/30, avg 9.17 Mbps, peak bandwidth utilization 95.5%).
- ✅ **Server mode recovered markedly** (86.28% → 92.98%): `amplificationlimit` (6/14 → 13/14), `rebind-addr` (10/14 → 14/14), `rebind-port` (10/14 → 13/14), and `handshakeloss` (10/13 → 12/13) returned to passing broadly; client-mode `amplificationlimit` (14/16 → 15/16) and `handshakeloss` (14/15 → 15/15) also improved.
- ⚠️ Main regression this round: `crosstraffic` 27/30 → 21/30 (client mode 14/16 → 8/16; `ngtcp2` / `go-x-net` / `quic-go` / `quiche` / `aioquic` / `nginx` missed the threshold).
- ⚠️ Current weak spots: `connectionmigration` (7/19, 36.8%), `rebind-addr` (22/30, 73.3%), `rebind-port` (23/30, 76.7%), server-mode `keyupdate` (8/10) and `zerortt` (10/12).

---

## 1. Overview

- **Source**: one full-matrix run of the upstream [quic-interop-runner](https://github.com/quic-interop/quic-interop-runner) (result files `quicx_as_client_full.json` / `quicx_as_server_full.json`, log dirs `logs_full_{client,server}_20261010_142617`).
- **Environment**: Linux host, Docker + ns-3 network simulation (leftnet ↔ sim ↔ rightnet); quicX runs from locally built `interop_{server,client}` binaries.
- **Coverage**: 24 scenarios × (16 server-side peers + 15 client-side peers + a quicX ↔ quicX self-check) = **768 combinations**; the self-check passed 21/21 in the server direction.
- **Delta vs. previous baseline**: the previous round used the same caliber without the self-check (744 cases, 90.60% effective). This round is up 2.27pp, driven mainly by server-mode recovery of `amplificationlimit`, `rebind-addr`, `rebind-port`, and `handshakeloss`; client-mode `amplificationlimit` and `handshakeloss` improved, and `connectionmigration` edged up (6/19 → 7/19). The regression concentrates in client-mode `crosstraffic` (14/16 → 8/16). The upstream matrix does not include `multiconnect` / `versionnegotiation`.

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
| Total cases | **768** |
| ✅ Passed | **625** |
| ❌ Failed | **48** |
| ➖ Unsupported | **95** |
| **Effective pass rate (excl. Unsupported)** | **625 / 673 ≈ 92.87%** |
| RFC conformance pass rate | 572 / 611 ≈ 93.62% (client 294/312 = 94.23%; server 278/299 = 92.98%) |
| Performance pass rate | 53 / 62 ≈ 85.48% (goodput 30/30; crosstraffic 21/30) |
| Pass rate incl. Unsupported | 625 / 768 ≈ 81.4% |

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

## 5. Peer ranking

| Rank | Implementation | Client mode (initiator) | Server mode (responder) | Protocol-feature pass rate | Combined effective pass rate | Status |
| :---: | :--- | :---: | :---: | :---: | :---: | :---: |
| 1 | **neqo** | 24/24 (100.0%) | 23/24 (95.8%) | **43/44 (97.7%)** | 47/48 (97.9%) | 🌟 Excellent |
| 2 | **picoquic** | 24/24 (100.0%) | 23/24 (95.8%) | **43/44 (97.7%)** | 47/48 (97.9%) | 🌟 Excellent |
| 3 | **lsquic** | 23/24 (95.8%) | 23/23 (100.0%) | **42/43 (97.7%)** | 46/47 (97.9%) | 🌟 Excellent |
| 4 | **aioquic** | 21/22 (95.5%) | 22/23 (95.7%) | **40/41 (97.6%)** | 43/45 (95.6%) | 🌟 Excellent |
| 5 | **msquic** | 21/21 (100.0%) | 20/22 (90.9%) | **37/39 (94.9%)** | 41/43 (95.3%) | 🌟 Excellent |
| 6 | **quic-go** | 20/21 (95.2%) | 20/22 (90.9%) | **37/39 (94.9%)** | 40/43 (93.0%) | 🌟 Excellent |
| 7 | **xquic** | 21/21 (100.0%) | 19/22 (86.4%) | **37/39 (94.9%)** | 40/43 (93.0%) | 🌟 Excellent |
| 8 | **ngtcp2** | 20/24 (83.3%) | 24/24 (100.0%) | **41/44 (93.2%)** | 44/48 (91.7%) | 🌟 Excellent |
| 9 | **quinn** | 22/22 (100.0%) | 21/24 (87.5%) | **39/42 (92.9%)** | 43/46 (93.5%) | 🌟 Excellent |
| 10 | **s2n-quic** | 20/22 (90.9%) | 19/20 (95.0%) | **35/38 (92.1%)** | 39/42 (92.9%) | 🌟 Excellent |
| 11 | **quiche** | 18/21 (85.7%) | 19/20 (95.0%) | **34/37 (91.9%)** | 37/41 (90.2%) | 🌟 Excellent |
| 12 | **kwik** | 19/22 (86.4%) | 21/23 (91.3%) | **37/41 (90.2%)** | 40/45 (88.9%) | 🌟 Excellent |
| 13 | **haproxy** | 20/22 (90.9%) | N/A | **18/20 (90.0%)** | 20/22 (90.9%) | 🌟 Excellent |
| 14 | **nginx** | 18/21 (85.7%) | N/A | **17/19 (89.5%)** | 18/21 (85.7%) | 👍 Good |
| 15 | **go-x-net** | 11/14 (78.6%) | 14/15 (93.3%) | **22/25 (88.0%)** | 25/29 (86.2%) | 👍 Good |
| 16 | **mvfst** | 16/19 (84.2%) | 14/18 (77.8%) | **27/33 (81.8%)** | 30/37 (81.1%) | 👍 Good |
| 17 | **chrome** | N/A | 1/1 (100.0%) | **1/1 (100.0%)** | 1/1 (100.0%) | 🌟 Excellent |

> Note: "client mode" means quicX as the client connecting to the peer's server; "server mode" means the peer as the client connecting to quicX's server. The "protocol-feature pass rate" excludes the goodput / crosstraffic performance scenarios.

---

## 6. Detailed per-scenario matrices

### 🔹 handshake (`H`)

> **Description**: Handshake completes successfully.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 transfer (`DC`)

> **Description**: Stream data is being sent and received correctly. Connection close completes with a zero error code.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 longrtt (`LR`)

> **Description**: Handshake completes when RTT is long.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 chacha20 (`C20`)

> **Description**: Handshake completes using ChaCha20.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ➖ | ✅ | ➖ | ➖ | ✅ | ✅ | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 multiplexing (`M`)

> **Description**: Thousands of files are transferred over a single connection, and server increased stream limits to accomodate client requests.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | — | — | ✅ | ➖ |

### 🔹 retry (`S`)

> **Description**: Server sends a Retry, and a subsequent connection using the Retry token completes successfully.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 resumption (`R`)

> **Description**: Connection is established using TLS Session Resumption.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 zerortt (`Z`)

> **Description**: 0-RTT data is being sent and acted on.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ➖ | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ❌ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 http3 (`3`)

> **Description**: An H3 transaction succeeded.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ➖ | ✅ | ✅ | — | — | ✅ | ✅ |

### 🔹 blackhole (`B`)

> **Description**: Transfer succeeds despite underlying network blacking out for a few seconds.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 keyupdate (`U`)

> **Description**: One of the two endpoints updates keys and the peer responds correctly.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ➖ | ➖ | ✅ | ➖ | ✅ | ➖ | ✅ | ✅ | ❌ | ✅ | — | — | ❌ | ➖ |

### 🔹 ecn (`E`)

> **Description**: Handshake completes successfully.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ➖ | ✅ | ➖ | ✅ | ✅ | ➖ | ➖ | ➖ | ➖ | ✅ | ➖ | ➖ | ➖ | ✅ | — |
| **quicX as Server** | ✅ | ➖ | ➖ | ✅ | ➖ | ✅ | ✅ | ➖ | ➖ | ➖ | ➖ | ✅ | ➖ | — | — | ✅ | ➖ |

### 🔹 amplificationlimit (`A`)

> **Description**: The server obeys the 3x amplification limit.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 handshakeloss (`L1`)

> **Description**: Handshake completes under extreme packet loss.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 transferloss (`L2`)

> **Description**: Transfer completes under moderate packet loss.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 handshakecorruption (`C1`)

> **Description**: Handshake completes under extreme packet corruption.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ➖ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 transfercorruption (`C2`)

> **Description**: Transfer completes under moderate packet corruption.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 ipv6 (`6`)

> **Description**: A transfer across an IPv6-only network succeeded.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 v2 (`V2`)

> **Description**: Server should select QUIC v2 in compatible version negotiation.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅ | ➖ | ➖ | ➖ | ➖ | ✅ | ✅ | ✅ | ➖ | ✅ | ✅ | ✅ | ➖ | ➖ | ✅ | ➖ | — |
| **quicX as Server** | ✅ | ➖ | ➖ | ➖ | ➖ | ✅ | ✅ | ✅ | ➖ | ✅ | ✅ | ✅ | ➖ | — | — | ❌ | ➖ |

### 🔹 rebind-port (`BP`)

> **Description**: Transfer completes under frequent port rebindings on the client side.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ❌ | ❌ | ✅ | ✅ | ❌ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | ❌ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 rebind-addr (`BA`)

> **Description**: Transfer completes under frequent IP address and port rebindings on the client side.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ❌ | ❌ | ✅ | ❌ | ❌ | ✅ | ✅ | ❌ | ❌ | ✅ | ✅ | ✅ | ✅ | ❌ | ❌ | ✅ | — |
| **quicX as Server** | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | — | — | ✅ | ➖ |

### 🔹 connectionmigration (`CM`)

> **Description**: A transfer succeeded during which the client performed an active migration.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ❌ | ➖ | ➖ | ❌ | ➖ | ✅ | ✅ | ➖ | ➖ | ➖ | ➖ | ✅ | ➖ | ➖ | ➖ | ➖ | — |
| **quicX as Server** | ✅ | ❌ | ❌ | ❌ | ❌ | ✅ | ✅ | ❌ | ❌ | ❌ | ❌ | ✅ | ❌ | — | — | ❌ | ➖ |

### 🔹 goodput (`G`)

> **Description**: Measures connection goodput over a 10Mbps link.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ✅<br><sub>9.31 Mbps</sub> | ✅<br><sub>8.07 Mbps</sub> | ✅<br><sub>9.51 Mbps</sub> | ✅<br><sub>9.26 Mbps</sub> | ✅<br><sub>9.45 Mbps</sub> | ✅<br><sub>8.41 Mbps</sub> | ✅<br><sub>9.55 Mbps</sub> | ✅<br><sub>8.76 Mbps</sub> | ✅<br><sub>9.35 Mbps</sub> | ✅<br><sub>8.64 Mbps</sub> | ✅<br><sub>8.59 Mbps</sub> | ✅<br><sub>9.27 Mbps</sub> | ✅<br><sub>9.36 Mbps</sub> | ✅<br><sub>9.47 Mbps</sub> | ✅<br><sub>9.15 Mbps</sub> | ✅<br><sub>8.76 Mbps</sub> | — |
| **quicX as Server** | ✅<br><sub>9.38 Mbps</sub> | ✅<br><sub>9.42 Mbps</sub> | ✅<br><sub>9.49 Mbps</sub> | ✅<br><sub>8.80 Mbps</sub> | ✅<br><sub>9.34 Mbps</sub> | ✅<br><sub>9.19 Mbps</sub> | ✅<br><sub>9.44 Mbps</sub> | ✅<br><sub>9.36 Mbps</sub> | ✅<br><sub>9.43 Mbps</sub> | ✅<br><sub>9.36 Mbps</sub> | ✅<br><sub>9.48 Mbps</sub> | ✅<br><sub>9.09 Mbps</sub> | ✅<br><sub>9.34 Mbps</sub> | — | — | ✅<br><sub>9.15 Mbps</sub> | ➖ |

#### 📊 Measured throughput ranking (10 Mbps link, average: **9.17 Mbps**)

| Rank | Peer | Role | Status | Detail (with jitter) | Throughput | Utilization |
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

> 💡 **Performance analysis**: quicX excels on the 10 Mbps bottleneck link — all 30 measured combinations in both directions (client mode 16/16, server mode 14/14) pass at **100%**, with peak bandwidth utilization of **95.5%** (`lsquic`: 9.55 Mbps) and a global average effective rate of **9.17 Mbps**.

### 🔹 crosstraffic (`C`)

> **Description**: Measures goodput over a 10Mbps link when competing with a TCP (cubic) connection.

| Role \ Peer | ngtcp2 | go-x-net | quic-go | s2n-quic | quiche | neqo | lsquic | kwik | mvfst | aioquic | msquic | picoquic | xquic | nginx | haproxy | quinn | chrome |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **quicX as Client** | ❌ | ❌ | ❌ | ✅<br><sub>4.84 Mbps</sub> | ❌ | ✅<br><sub>4.04 Mbps</sub> | ✅<br><sub>8.36 Mbps</sub> | ❌ | ❌ | ❌ | ✅<br><sub>3.28 Mbps</sub> | ✅<br><sub>7.20 Mbps</sub> | ✅<br><sub>8.57 Mbps</sub> | ❌ | ✅<br><sub>4.75 Mbps</sub> | ✅<br><sub>3.03 Mbps</sub> | — |
| **quicX as Server** | ✅<br><sub>4.13 Mbps</sub> | ✅<br><sub>4.81 Mbps</sub> | ✅<br><sub>4.95 Mbps</sub> | ✅<br><sub>4.66 Mbps</sub> | ✅<br><sub>5.46 Mbps</sub> | ✅<br><sub>4.70 Mbps</sub> | ✅<br><sub>5.15 Mbps</sub> | ✅<br><sub>5.69 Mbps</sub> | ✅<br><sub>6.16 Mbps</sub> | ✅<br><sub>7.19 Mbps</sub> | ✅<br><sub>6.45 Mbps</sub> | ✅<br><sub>4.87 Mbps</sub> | ❌ | — | — | ✅<br><sub>5.33 Mbps</sub> | ➖ |

#### 📊 TCP (Cubic) competition detail

| Rank | Peer | Role | Status | Detail (with jitter) | Throughput | Note |
| :---: | :--- | :---: | :---: | :---: | :---: | :--- |
| 1 | **xquic** | quicX as Client | ✅ | 8570 (± 51) kbps | **8.57 Mbps** | Fair bandwidth share on the 10 Mbps competing link (utilization `85.7%`) |
| 2 | **lsquic** | quicX as Client | ✅ | 8361 (± 158) kbps | **8.36 Mbps** | Fair bandwidth share on the 10 Mbps competing link (utilization `83.6%`) |
| 3 | **picoquic** | quicX as Client | ✅ | 7203 (± 264) kbps | **7.20 Mbps** | Fair bandwidth share on the 10 Mbps competing link (utilization `72.0%`) |
| 4 | **aioquic** | quicX as Server | ✅ | 7194 (± 451) kbps | **7.19 Mbps** | Fair bandwidth share on the 10 Mbps competing link (utilization `71.9%`) |
| 5 | **msquic** | quicX as Server | ✅ | 6445 (± 446) kbps | **6.45 Mbps** | Fair bandwidth share on the 10 Mbps competing link (utilization `64.5%`) |
| 6 | **mvfst** | quicX as Server | ✅ | 6164 (± 223) kbps | **6.16 Mbps** | Fair bandwidth share on the 10 Mbps competing link (utilization `61.6%`) |
| 7 | **kwik** | quicX as Server | ✅ | 5687 (± 230) kbps | **5.69 Mbps** | Fair bandwidth share on the 10 Mbps competing link (utilization `56.9%`) |
| 8 | **quiche** | quicX as Server | ✅ | 5460 (± 556) kbps | **5.46 Mbps** | Fair bandwidth share on the 10 Mbps competing link (utilization `54.6%`) |
| 9 | **quinn** | quicX as Server | ✅ | 5328 (± 596) kbps | **5.33 Mbps** | Fair bandwidth share on the 10 Mbps competing link (utilization `53.3%`) |
| 10 | **lsquic** | quicX as Server | ✅ | 5150 (± 204) kbps | **5.15 Mbps** | Fair bandwidth share on the 10 Mbps competing link (utilization `51.5%`) |
| 11 | **quic-go** | quicX as Server | ✅ | 4953 (± 544) kbps | **4.95 Mbps** | Fair bandwidth share on the 10 Mbps competing link (utilization `49.5%`) |
| 12 | **picoquic** | quicX as Server | ✅ | 4865 (± 293) kbps | **4.87 Mbps** | Fair bandwidth share on the 10 Mbps competing link (utilization `48.6%`) |
| 13 | **s2n-quic** | quicX as Client | ✅ | 4845 (± 191) kbps | **4.84 Mbps** | Fair bandwidth share on the 10 Mbps competing link (utilization `48.4%`) |
| 14 | **go-x-net** | quicX as Server | ✅ | 4812 (± 59) kbps | **4.81 Mbps** | Fair bandwidth share on the 10 Mbps competing link (utilization `48.1%`) |
| 15 | **haproxy** | quicX as Client | ✅ | 4746 (± 158) kbps | **4.75 Mbps** | Fair bandwidth share on the 10 Mbps competing link (utilization `47.5%`) |
| 16 | **neqo** | quicX as Server | ✅ | 4701 (± 496) kbps | **4.70 Mbps** | Fair bandwidth share on the 10 Mbps competing link (utilization `47.0%`) |
| 17 | **s2n-quic** | quicX as Server | ✅ | 4661 (± 332) kbps | **4.66 Mbps** | Fair bandwidth share on the 10 Mbps competing link (utilization `46.6%`) |
| 18 | **ngtcp2** | quicX as Server | ✅ | 4127 (± 588) kbps | **4.13 Mbps** | Fair bandwidth share on the 10 Mbps competing link (utilization `41.3%`) |
| 19 | **neqo** | quicX as Client | ✅ | 4041 (± 172) kbps | **4.04 Mbps** | Fair bandwidth share on the 10 Mbps competing link (utilization `40.4%`) |
| 20 | **msquic** | quicX as Client | ✅ | 3282 (± 78) kbps | **3.28 Mbps** | Fair bandwidth share on the 10 Mbps competing link (utilization `32.8%`) |
| 21 | **quinn** | quicX as Client | ✅ | 3030 (± 339) kbps | **3.03 Mbps** | Fair bandwidth share on the 10 Mbps competing link (utilization `30.3%`) |
| - | `ngtcp2`, `go-x-net`, `quic-go`, `quiche`, `kwik`, `mvfst`, `aioquic`, `nginx` | quicX as Client | ❌ | — | — | Throughput below the interop-runner threshold under TCP Cubic competition |
| - | `xquic` | quicX as Server | ❌ | — | — | Throughput below the interop-runner threshold under TCP Cubic competition |

> 💡 **Competition analysis**: as the server, quicX shows steady congestion resilience and bandwidth contention — 13/14 (92.9%) of peer clients meet the threshold (best: `aioquic` 7.19 Mbps, `msquic` 6.45 Mbps, `mvfst` 6.16 Mbps), with only `xquic` missing it. As the client, 8/16 (50.0%) meet the threshold (best contention efficiency: `xquic` 8.57 Mbps, `lsquic` 8.36 Mbps, `picoquic` 7.20 Mbps); `ngtcp2`, `go-x-net`, `quic-go`, `quiche`, `kwik`, `mvfst`, `aioquic`, and `nginx` miss the threshold. Overall TCP Cubic competition pass rate is **70.0% (21/30)** — client-mode competition resilience is the main weak spot of this round.
