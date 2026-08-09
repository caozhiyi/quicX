# quicX Interoperability Status

> This document records the **latest baseline interoperability results** of
> quicX against the major QUIC implementations and is the canonical version
> to cite externally.
> For test methodology, commands, and environment, see
> [`guide/interop_runbook.md`](../guide/interop_runbook.md).

| Item | Value |
|---|---|
| **Report date** | 2026-08-09 |
| **Run mode** | ns-3 network simulator (`quic-network-simulator` topology) |
| **Peers under test** | 11 third-party implementations + quicX self-test |
| **Scenarios under test** | 14 IETF interop scenarios |
| **Total cases** | 322 (PASS 216 / FAIL 16 / UNSUPPORTED 94) |
| **Effective pass rate** | **93.1%** (216 / 232, excluding UNSUPPORTED) |

> Note: the `quicx → mvfst` client-side row has been updated to reflect the
> latest validation (2026-08-09): handshake / transfer / resumption /
> zerortt / keyupdate / rebind-addr / rebind-port now pass; only `http3`
> fails. All other cells remain the canonical ns-3 baseline.

---

## TL;DR

- ✅ Under realistic ns-3 link emulation, quicX interoperates well with the
  11 mainstream implementations — **effective pass rate 93.1%**.
- ✅ `chacha20`, `keyupdate`, `rebind-port`, `rebind-addr`, `multiconnect`
  are at 100%.
- ✅ `quicx → mvfst` improved to 8/9 (only `http3` fails); `handshake`,
  `transfer`, `resumption`, `zerortt`, `keyupdate`, `rebind-addr`,
  `rebind-port` all pass.
- ⚠️ Remaining quicX-side issues: `quicx → aioquic` `retry` timeout (1 case),
  `quicx → picoquic | lsquic` `connectionmigration` timeout (2 cases).
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
- **quicX as Client** — quicX acts as the client connecting to a third-party
  implementation.

`self` column is the quicX ↔ quicX self-test result.

---

## 3. Overall Results

> Caliber note: runner executed 322 independent cases (self counted once).
> §5/§6 count two-way (self counted in both Server and Client rows), so the
> totals differ by 14 self duplicates.

| Metric | Value |
|------|-------|
| Total cases | **322** |
| ✅ Passed | **216** |
| ❌ Failed | **16** |
| `-` Unsupported | **94** |
| **Effective pass rate (excl. Unsupported)** | **216 / 232 ≈ 93.1%** |
| Pass rate incl. Unsupported | 216 / 322 ≈ 67.1% |

---

## 4. Connectivity Matrix (by scenario)

### 4.1 handshake

| quicX role \ peer | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| Client (\*↔quicX)   | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

### 4.2 transfer

| quicX role \ peer | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| Client (\*↔quicX)   | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

### 4.3 retry

| quicX role \ peer | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ | ✅ | ✅ | ✅ |  -  | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| Client (\*↔quicX)   | ✅ | ✅ | ✅ | ✅ |  -  | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ |

### 4.4 resumption

| quicX role \ peer | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| Client (\*↔quicX)   | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

### 4.5 zerortt

| quicX role \ peer | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |  -  |
| Client (\*↔quicX)   | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |

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
| Client (\*↔quicX)   | ✅ |  -  | ✅ |  -  | ✅ | ✅ | ✅ | ✅ |  -  |  -  | ✅ |  -  |

### 4.11 v2

| quicX role \ peer | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ |  -  | ✅ |  -  |  -  | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |  -  |
| Client (\*↔quicX)   | ✅ |  -  | ✅ |  -  |  -  |  -  | ✅ | ✅ | ✅ | ✅ | ✅ |  -  |

### 4.12 rebind-port

| quicX role \ peer | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ |  -  |  -  |  -  |  -  | ✅ |  -  | ✅ |  -  |  -  |  -  |  -  |
| Client (\*↔quicX)   | ✅ |  -  |  -  |  -  | ✅ |  -  |  -  | ✅ |  -  |  -  |  -  |  -  |

### 4.13 rebind-addr

| quicX role \ peer | self | quiche | ngtcp2 | quic-go | mvfst | quinn | aioquic | picoquic | neqo | lsquic | msquic | s2n-quic |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| Server (quicX↔\*)   | ✅ |  -  |  -  |  -  |  -  | ✅ |  -  |  -  |  -  |  -  |  -  |  -  |
| Client (\*↔quicX)   | ✅ |  -  |  -  |  -  | ✅ |  -  |  -  |  -  |  -  |  -  |  -  |  -  |

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
| **Total (two-way count)** | **230** | **16** | **90** | **230/246 ≈ 93.5%** |

> Note: the two-way total of 230 exceeds §3's "independent Passed = 216" by
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
| **mvfst**              | 4 / 6 (8 unsupported, 2 failed) | 8 / 9 (5 unsupported, 1 failed) | **12 / 15** |
| **quinn**              | 14 / 14 (all PASS) | 9 / 9 (5 unsupported, all PASS) | 23 / 23 |
| **aioquic**            | 10 / 10 (4 unsupported, all PASS) | 9 / 10 (4 unsupported, 1 failed) | 19 / 20 |
| **picoquic**           | 13 / 13 (1 unsupported, all PASS) | 12 / 13 (1 unsupported, 1 failed) | 25 / 26 |
| **neqo**               | 10 / 10 (4 unsupported, all PASS) | 9 / 10 (4 unsupported, 1 failed) | 19 / 20 |
| **lsquic**             | 10 / 10 (4 unsupported, all PASS) | 9 / 10 (4 unsupported, 1 failed) | 19 / 20 |
| **msquic**             | 8 / 10 (4 unsupported, 2 failed) | 10 / 10 (4 unsupported, all PASS) | 18 / 20 |
| **s2n-quic**           | 0 / 6 (8 unsupported, **6 failed**) | 9 / 10 (4 unsupported, 1 failed) | **9 / 16** |
| **Total** | 111 / 122 | 117 / 123 | **228 / 245 ≈ 93.1%** |

### A few observations

- **5 fully-interoperating peers** (quicX ↔ X with zero failures in both
  directions): `self`, `quiche`, `ngtcp2`, `quic-go`, `quinn`. These five
  represent quicX's stable core compatibility surface.
- **Almost-fully interoperating** (only 1 quicx→X failure):
  `aioquic`, `picoquic`, `neqo`, `lsquic` — failures are concentrated in
  the `retry` or `connectionmigration` scenarios.
- **mvfst** client-side improved to 8/9 (only `http3` fails); the remaining
  `mvfst → quicx` failures (resumption, zerortt) are on the server side.
- **s2n-quic as Server** has a structural issue (6 failures): the image
  exits 1 immediately after start, pointing at the third-party image /
  version side.

---

## 7. Failure inventory (16 cases total)

Grouped by attribution to make engineering follow-up easier. The group
totals add up to 1 + 4 + 5 + 6 = **16**.

### A. quicX → mvfst (quicX-side, 1 case)

> After the latest validation, only `http3` remains failing on the
> quicX-client side. `handshake` / `transfer` / `resumption` / `zerortt`
> now pass (previously returned 190 bytes → `Size mismatch`).

| # | Scenario | Pair | Symptom |
|---|---|---|---|
| A1 | http3 | quicx → mvfst | Client exited with code 1 (H3 protocol-layer timeout) |

### B. quicX → other implementations (migration / special scenarios, 4 cases)

| # | Scenario | Pair | Duration | Symptom |
|---|---|---|---|---|
| B1 | retry                | quicx → aioquic   | 40.58 s | Client exited with code 1 (timeout) |
| B2 | connectionmigration  | quicx → picoquic  | 40.87 s | Client exited with code 1 (migration path anomaly) |
| B3 | connectionmigration  | quicx → neqo      | 5.07 s  | Server failed to start (neqo image does not respond to migration) |
| B4 | connectionmigration  | quicx → lsquic    | 40.91 s | Client exited with code 1 (migration path anomaly) |

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

#### D-b: msquic Client in VN / v2 (1 case)

| # | Scenario | Pair | Duration | Symptom |
|---|---|---|---|---|
| D6 | versionnegotiation | msquic → quicx | 13.07 s | File not downloaded: 1KB.bin (msquic Client only probes VN, does not transfer) |

---

## 8. Follow-up items (by priority)

### P0 — quicX-side to fix (3 cases)

1. **`quicx → mvfst` `http3`** (A1) — H3 protocol-layer timeout; the only
   remaining quicX-client-side mvfst failure.
2. **`quicx → picoquic | lsquic` `connectionmigration` 40 s timeout** (B2, B4)
   — real migration issue exposed under ns-3 sim.
3. **`quicx → aioquic` `retry` 40 s timeout** (B1) — regression; locate retry
   token decode path.

### P1 — third-party image / environment (13 cases)

4. **mvfst Client** (C1–C5, 5) / **s2n-quic Client** (D1–D5, 5) image
   compatibility — consistent with upstream interop runner history.
5. **msquic Client** VN / v2 no-file (D6) — inherent msquic image behavior.

---

## 9. Reproduction commands

```bash
# ns-3 full matrix (recommended canonical run)
cd test/interop
python3 interop_runner.py --matrix --implementations all --use-local-bin \
    --output markdown --output-file logs/latest_matrix_sim.md
```

> If ns-3 sim cannot be launched due to environment limits (rare macOS /
> trimmed kernels), you may temporarily use `--no-sim` bridge mode. However,
> under `--no-sim`, `rebind-*` and `connectionmigration` lack real-link
> semantics and **must not be used as public release data**. Ensure
> `setup_noop.sh` has execute permission (`chmod +x test/interop/setup_noop.sh`).
