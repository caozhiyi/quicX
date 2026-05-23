# quicX Interoperability Status

> This document records the **latest baseline interoperability results** of
> quicX against the major QUIC implementations and is the canonical version
> to cite externally.
> For test methodology, commands, and environment, see
> [`guide/interop_runbook.md`](../guide/interop_runbook.md).

| Item | Value |
|---|---|
| **Report date** | 2026-05-23 |
| **Run mode** | ns-3 network simulator (`quic-network-simulator` topology) |
| **Peers under test** | 11 third-party implementations + quicX self-test |
| **Scenarios under test** | 14 IETF interop scenarios |
| **Total cases** | 322 (PASS 208 / FAIL 20 / UNSUPPORTED 94) |
| **Effective pass rate** | **91.2%** (208 / 228, excluding UNSUPPORTED) |

---

## TL;DR

- ✅ Under realistic ns-3 link emulation, quicX interoperates well with the
  11 mainstream implementations — **effective pass rate 91.2%**.
- ✅ `chacha20`, `keyupdate`, `rebind-port`, `rebind-addr`, `multiconnect`
  are at or near 100%.
- ⚠️ **3 classes of issues that quicX itself needs to follow up on**:
  1. `quicx → mvfst` H3 / Transfer path consistently receives 190 bytes (5 cases)
  2. `quicx → picoquic | lsquic` `connectionmigration` 40 s timeout
     (2 cases, first exposed in sim mode)
  3. `quicx → aioquic` `retry` 40 s timeout (1 case, regression)
- 🔵 The remaining failures (mvfst Client / s2n-quic Client / msquic VN&v2)
  are third-party image compatibility issues, tracked upstream.

---

## 1. Overview

- **Command used**:
  ```bash
  python3 interop_runner.py --matrix --implementations all --use-local-bin
  ```
- **Environment**: Linux host, Docker + Compose v2.24+, **ns-3 simulator
  mode** (`docker-compose.yml`, `leftnet 193.167.0.0/24` ↔ `sim` ↔
  `rightnet 193.167.100.0/24`, containers granted `NET_ADMIN` + `NET_RAW`).
  quicX runs from local binaries (`build/bin/interop_{server,client}`).
- **Coverage**: 14 scenarios × 23 effective peer combinations (quicX ↔ 11
  third-party implementations in both directions, plus quicX ↔ quicX
  self-test).
- **Wall-clock duration**: ~55 minutes.

## 2. Legend

| Mark | Meaning |
|:----:|---------|
| ✅ | PASSED — client exits 0, downloaded files match byte-by-byte |
| ❌ | FAILED — process error / file hash mismatch / timeout |
| `-` | UNSUPPORTED — one side declared unsupported, or the image cannot start under the current sim network |

For each scenario, two direction-specific matrices are listed:

- **quicX as Server** — third-party implementation acts as the client
  connecting to quicX.
- **quicX as Client** — quicX acts as the client connecting to a
  third-party server.

The `self` column is the quicX ↔ quicX self-test result.

---

## 3. Aggregate result

> Counting convention: the runner executes 322 independent cases (14 of
> which are self-tests). §3 "Passed 208" is the per-case count; §5 and §6
> use the "two-way expanded" count (the self-test is counted once in the
> Server row and once in the Client row), so the row total there is 222.
> The two conventions are equivalent once the 14 self-test duplicates are
> removed.

| Metric | Value |
|--------|-------|
| Total tests | **322** |
| ✅ Passed | **208** |
| ❌ Failed | **20** |
| `-` Unsupported | **94** |
| Skipped | 0 |
| **Effective pass rate (excluding Unsupported)** | **208 / 228 ≈ 91.2%** |
| Pass rate including Unsupported | 208 / 322 ≈ 64.6% |

---

## 4. Connectivity matrix per scenario

### 4.1 handshake

| quicX role \ peer | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| Client (\*↔quicX)   | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

### 4.2 transfer

| quicX role \ peer | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| Client (\*↔quicX)   | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

### 4.3 retry

| quicX role \ peer | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ | ✅ | ✅ | ✅ |  -  | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| Client (\*↔quicX)   | ✅ | ✅ | ✅ | ✅ |  -  | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ |

### 4.4 resumption

| quicX role \ peer | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| Client (\*↔quicX)   | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

### 4.5 zerortt

| quicX role \ peer | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |  -  |
| Client (\*↔quicX)   | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |

### 4.6 http3

| quicX role \ peer | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |  -  | ❌ |
| Client (\*↔quicX)   | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ |  -  | ✅ |

### 4.7 multiconnect

| quicX role \ peer | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| Client (\*↔quicX)   | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

### 4.8 versionnegotiation

| quicX role \ peer | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ |  -  | ✅ | ✅ |  -  | ✅ |  -  | ✅ |  -  |  -  | ❌ |  -  |
| Client (\*↔quicX)   | ✅ |  -  | ✅ | ✅ |  -  |  -  |  -  | ✅ |  -  |  -  | ✅ | ✅ |

### 4.9 chacha20

| quicX role \ peer | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ |  -  | ✅ | ✅ |  -  | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |  -  |
| Client (\*↔quicX)   | ✅ | ✅ | ✅ | ✅ |  -  | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

### 4.10 keyupdate

| quicX role \ peer | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ |  -  | ✅ | ✅ |  -  | ✅ | ✅ | ✅ | ✅ |  -  | ✅ |  -  |
| Client (\*↔quicX)   | ✅ |  -  | ✅ |  -  |  -  | ✅ | ✅ | ✅ |  -  |  -  | ✅ |  -  |

### 4.11 v2

| quicX role \ peer | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ |  -  | ✅ |  -  |  -  | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |  -  |
| Client (\*↔quicX)   | ✅ |  -  | ✅ |  -  |  -  |  -  | ✅ | ✅ | ✅ | ✅ | ✅ |  -  |

### 4.12 rebind-port

| quicX role \ peer | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ |  -  |  -  |  -  |  -  | ✅ |  -  | ✅ |  -  |  -  |  -  |  -  |
| Client (\*↔quicX)   | ✅ |  -  |  -  |  -  |  -  |  -  |  -  | ✅ |  -  |  -  |  -  |  -  |

### 4.13 rebind-addr

| quicX role \ peer | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ |  -  |  -  |  -  |  -  | ✅ |  -  |  -  |  -  |  -  |  -  |  -  |
| Client (\*↔quicX)   | ✅ |  -  |  -  |  -  |  -  |  -  |  -  |  -  |  -  |  -  |  -  |  -  |

### 4.14 connectionmigration

| quicX role \ peer | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ |  -  | ✅ |  -  |  -  | ✅ |  -  | ✅ |  -  | ✅ |  -  |  -  |
| Client (\*↔quicX)   | ✅ |  -  |  -  |  -  |  -  |  -  |  -  | ❌ | ❌ | ❌ |  -  | ✅ |

> Note: the heavy `-` density in `rebind-*` / `connectionmigration` is because
> several third-party images do not declare support for those scenarios in the
> IETF interop matrix (the runner marks them UNSUPPORTED outright). This is
> standard for the official matrix.

---

## 5. Per-scenario summary

| Scenario | ✅ Pass | ❌ Fail | `-` Unsupported | Effective pass rate |
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
| **Total (two-way count)** | **222** | **20** | **94** | **222/242 ≈ 91.7%** |

> Note: the two-way total of 222 exceeds §3's "independent Passed = 208" by
> 14, because the self-test is counted once in the Server row and once in
> the Client row. After removing the 14 self-test duplicates the two
> conventions are equivalent.

---

## 6. Per-implementation summary (quicX-centric)

> Format: `a / b (c unsupported, d failed)` — across 14 scenarios, `b` ran
> effectively (PASS+FAIL); `a` of those passed, `d` failed; `c` were marked
> UNSUPPORTED by either side.

| Peer | quicX as Server (X→quicx) | quicX as Client (quicx→X) | Total pass / total effective |
|---|:---:|:---:|:---:|
| **self** (quicx↔quicx) | 14/14 (all PASS) | 14/14 (all PASS) | 14 / 14 |
| **quiche**             | 7 / 7 (7 unsupported, all PASS) | 8 / 8 (6 unsupported, all PASS) | 15 / 15 |
| **ngtcp2**             | 12 / 12 (2 unsupported, all PASS) | 11 / 11 (3 unsupported, all PASS) | 23 / 23 |
| **quic-go**            | 10 / 10 (4 unsupported, all PASS) | 9 / 9 (5 unsupported, all PASS) | 19 / 19 |
| **mvfst**              | 4 / 6 (8 unsupported, 2 failed) | 1 / 6 (8 unsupported, 5 failed) | **5 / 12** |
| **quinn**              | 14 / 14 (all PASS) | 9 / 9 (5 unsupported, all PASS) | 23 / 23 |
| **aioquic**            | 10 / 10 (4 unsupported, all PASS) | 9 / 10 (4 unsupported, 1 failed) | 19 / 20 |
| **picoquic**           | 13 / 13 (1 unsupported, all PASS) | 12 / 13 (1 unsupported, 1 failed) | 25 / 26 |
| **neqo**               | 10 / 10 (4 unsupported, all PASS) | 9 / 10 (4 unsupported, 1 failed) | 19 / 20 |
| **lsquic**             | 10 / 10 (4 unsupported, all PASS) | 9 / 10 (4 unsupported, 1 failed) | 19 / 20 |
| **msquic**             | 8 / 10 (4 unsupported, 2 failed) | 10 / 10 (4 unsupported, all PASS) | 18 / 20 |
| **s2n-quic**           | 0 / 6 (8 unsupported, **6 failed**) | 9 / 10 (4 unsupported, 1 failed) | **9 / 16** |
| **Total** | 111 / 122 | 110 / 120 | **222 / 242 ≈ 91.7%** |

### A few observations

- **5 fully-interoperating peers** (quicX ↔ X with zero failures in both
  directions): `self`, `quiche`, `ngtcp2`, `quic-go`, `quinn`. These five
  represent quicX's stable core compatibility surface.
- **Almost-fully interoperating** (only 1 quicx→X failure):
  `aioquic`, `picoquic`, `neqo`, `lsquic` — failures are concentrated in
  the `retry` or `connectionmigration` scenarios.
- **mvfst** is the largest source of failures: 5 H3/Transfer failures
  in `quicx → mvfst` (see §7 group A) and 5 in `mvfst → quicx`.
- **s2n-quic as Server** has a structural issue (6 failures): the image
  exits 1 immediately after start, pointing at the third-party image /
  version side.

---

## 7. Failure inventory (20 cases total)

Grouped by attribution to make engineering follow-up easier. The group
totals add up to 5 + 4 + 5 + 6 = **20**.

### A. quicX → mvfst (quicX-side, 5 cases)

> Common symptom: after handshake or connect, the download yields
> 190 bytes followed by `Size mismatch`. Suspected cause: quicX's client
> is not strict on mvfst's default STREAM/H3 frame combination, or
> ALPN/SNI lands on the mvfst image's "error page" template.

| # | Scenario | Pair | Duration | Symptom |
|---|---|---|---|---|
| A1 | handshake     | quicx → mvfst | 10.69 s | Size mismatch: 1KB.bin (expected 1024, got 190) |
| A2 | transfer      | quicx → mvfst | 10.75 s | Size mismatch: 1MB / 5MB.bin (got 190) |
| A3 | resumption    | quicx → mvfst | 12.02 s | Size mismatch: 1KB.bin (got 190) |
| A4 | zerortt       | quicx → mvfst | 11.93 s | Size mismatch: 1KB.bin (got 190) |
| A5 | http3         | quicx → mvfst | 10.14 s | Client exited with code 1 |

### B. quicX → other implementations (migration / special scenarios, 4 cases)

| # | Scenario | Pair | Duration | Symptom |
|---|---|---|---|---|
| B1 | retry                | quicx → aioquic   | 40.58 s | Client exited with code 1 (timeout) |
| B2 | connectionmigration  | quicx → picoquic  | 40.87 s | Client exited with code 1 (migration path anomaly) |
| B3 | connectionmigration  | quicx → neqo      | 5.07 s  | Server failed to start (neqo image does not respond to migration) |
| B4 | connectionmigration  | quicx → lsquic    | 40.91 s | Client exited with code 1 (migration path anomaly) |

> B2 / B4 are real migration issues first exposed under ns-3 sim mode and
> deserve focus; B1 is a regression; B3 is image-side.

### C. mvfst → quicX (mvfst client capability, 5 cases)

> mvfst Client typically exits in 7–8 s. This may relate to the
> fizz/fbthrift compile flags inside the mvfst image and is a long-standing
> upstream-side weakness.

| # | Scenario | Pair | Duration | Symptom |
|---|---|---|---|---|
| C1 | handshake   | mvfst → quicx | —    | Client exit 1 |
| C2 | transfer    | mvfst → quicx | —    | Client exit 1 |
| C3 | resumption  | mvfst → quicx | 7.53 s | File not downloaded: 1KB.bin |
| C4 | zerortt     | mvfst → quicx | 7.61 s | File not downloaded: 1KB.bin |
| C5 | http3       | mvfst → quicx | —    | Client exit 1 |

### D. Third-party Server / Client image issues (6 cases)

#### D-a: s2n-quic Client → quicX Server (5 cases)

> All fail in roughly 7 s. The s2n-quic client image is incompatible with
> the quicX server; not a quicX-side issue.

| # | Scenario | Pair | Duration | Symptom |
|---|---|---|---|---|
| D1 | handshake     | s2n-quic → quicx | 7.59 s | Client exited with code 1 |
| D2 | transfer      | s2n-quic → quicx | 7.73 s | Client exited with code 1 |
| D3 | retry         | s2n-quic → quicx | 7.65 s | Client exited with code 1 |
| D4 | resumption    | s2n-quic → quicx | 7.69 s | First connection failed (exit 1) |
| D5 | multiconnect  | s2n-quic → quicx | 7.96 s | Only 0/5 connections succeeded |

> Note: the previous `--no-sim` run had `http3 / s2n-quic → quicx` failing,
> but the runner classifies it UNSUPPORTED in this run — so it is not
> counted as a failure here.

#### D-b: msquic Client does not download files in VN / v2 scenarios (1 case → effectively 2 aggregated)

| # | Scenario | Pair | Duration | Symptom |
|---|---|---|---|---|
| D6 | versionnegotiation | msquic → quicx | 13.07 s | File not downloaded: 1KB.bin (msquic client only probes VN, no transfer) |

> Unrelated to the quicX server. The msquic image does not perform a data
> download in the VN scenario; the `v2` failure shares the same root cause
> and is not listed separately.

---

## 8. Open follow-ups (by priority)

### P0 — quicX-side fixes needed (8 cases total)

1. **`quicx → mvfst` H3 / Transfer path returns 190 bytes** (A1–A5, 5 cases)
   - Symptom: a fixed 190-byte response followed by `Size mismatch`
   - Hypothesis: H3 SETTINGS / HEADERS frame parsing is too lenient, or
     ALPN/SNI hits the mvfst image error-page template
   - Action: capture server qlog + client qlog from any one case and
     diff side-by-side
2. **`quicx → picoquic | lsquic` `connectionmigration` 40 s timeout**
   (B2, B4, 2 cases)
   - Real migration issues first exposed in sim mode
   - Action: compare PATH_CHALLENGE / PATH_RESPONSE timing against the
     `quicx ↔ quicx self` reference
3. **`quicx → aioquic` `retry` 40 s timeout** (B1, 1 case)
   - Behaves the same as the previous `--no-sim` run — a known regression
   - Action: trace the retry-token decode path

### P1 — third-party image / environment side (11 cases total)

4. **mvfst Client (C1–C5, 5 cases) / s2n-quic Client (D1–D5, 5 cases)
   image compatibility**
   - Consistent with the historical trend in the official interop runner;
     the upstream images are weak in this area long-term.
5. **msquic Client does not download in `versionnegotiation` / `v2`**
   (D6 + v2, 2 cases)
   - The msquic image treats VN/v2 as a probe-only scenario; the runner
     test logic and the image expectation disagree.

### P2 — minor (1 case)

6. **`connectionmigration / quicx → neqo`: Server failed to start** (B3)
   - Re-test after pulling the latest image in the next round.

---

## 9. Reproducing the matrix

Full sim-mode matrix (recommended, this report was produced by this
command):

```bash
# Prereqs: Linux host with IP forwarding enabled, /dev/net/tun available,
#          quicx-sim:latest image present
cd test/interop
python3 interop_runner.py --matrix --implementations all --use-local-bin \
    --output markdown --output-file logs/latest_matrix_sim.md
```

Run all scenarios against a single peer (e.g. quicX vs picoquic):

```bash
python3 interop_runner.py --client quicx --server picoquic --use-local-bin
python3 interop_runner.py --client picoquic --server quicx --use-local-bin
```

> If ns-3 sim cannot be brought up due to environment limits (some macOS
> or stripped-down kernels), `--no-sim` bridge mode can be used as a quick
> baseline. However, under `--no-sim` the `*loss` / `*corruption` /
> `rebind-*` / `connectionmigration` scenarios do not have realistic-link
> semantics and **must not be cited externally as official numbers**.

---
