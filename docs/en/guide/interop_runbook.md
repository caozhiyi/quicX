# QuicX Interop Test Usage

> This document describes **how to run** QuicX's interop tests.
> Status & result matrix lives in [`reports/interop_status.md`](../reports/interop_status.md).
> Framework internals & official spec live in [`guide/interop_overview.md`](./interop_overview.md).

---

## 1. Test scripts layout

```
test/interop/
├── interop_runner.py       # Entry point
├── testcases.py            # 14 scenario definitions
├── implementations.json    # Manifest of integrated peers
├── run_endpoint.sh         # In-container startup script for QuicX
├── certs/                  # Self-signed certificates (auto-generated on first run)
├── www/                    # Random test payloads (auto-generated on first run)
└── logs/                   # Per-run qlog / keylog / stdout / stderr
```

---

## 2. Prerequisites

### 2.1 Host dependencies

| Tool | Version | Notes |
|------|---------|-------|
| Docker | any recent | Used to pull peer images |
| Docker Compose | **v2.24+**, v2.36+ recommended | Needs `priority` / `interface_name` |
| Python | 3.8+ | For running `interop_runner.py` |
| openssl | - | For self-signed cert generation |
| CMake / C++17 toolchain | - | Only for `--use-local-bin` or `--local` |

### 2.2 First-run conveniences

- Random payloads are generated under `test/interop/www/` (1 KB / 1 MB / 5 MB …)
- Self-signed TLS certs are generated under `test/interop/certs/`
- Required Docker images are pulled / built (first run can take several minutes; subsequent runs hit the local cache)

### 2.3 Network mode

| Mode | Flag | Notes |
|------|------|-------|
| ns-3 simulated network | default | Uses the official `quic-network-simulator`; can inject delay / loss / reordering; Linux host required |
| Direct bridge | `--no-sim` | Skips ns-3, client and server talk via the `quicnet` bridge directly. **Required on macOS Docker Desktop**; faster and more stable on Linux too |

### 2.4 QuicX binary source

| Mode | Flag | Notes |
|------|------|-------|
| In-image binary | default | Use whatever was built inside `quicx-interop:latest` |
| Local-binary mount | `--use-local-bin` | Mount `build/bin/interop_{server,client}` into the QuicX container — **fastest validation loop after a C++ change** |
| Pure local processes | `--local` | No Docker; run two local binaries directly. Only useful for the QuicX ↔ QuicX self-test |

---

## 3. Building the local interop binaries

When `--use-local-bin` or `--local` is in play, build them once:

```bash
cd /data/workspace/quicX

cmake -S . -B build -DQUICX_BUILD_INTEROP=ON
cmake --build build --target interop_server interop_client -j
```

Produces:
- `build/bin/interop_server`
- `build/bin/interop_client`

---

## 4. Standard test commands

### 4.1 Full matrix (recommended)

A single command runs QuicX in both directions against every integrated peer:

```bash
cd /data/workspace/quicX/test/interop

python3 interop_runner.py \
    --matrix \
    --implementations all \
    --no-sim \
    --use-local-bin
```

| Flag | Meaning |
|------|---------|
| `--matrix` | Iterate every (server, client) combination |
| `--implementations all` | Cover every entry in `implementations.json` |
| `--no-sim` | Skip ns-3, use the Docker bridge |
| `--use-local-bin` | Use the host-side QuicX binaries; no need to rebuild the image |

By default the matrix is QuicX ↔ third-party only (it does not run
third-party ↔ third-party). Add `--full-matrix` for the full grid (much
slower).

### 4.2 A specific subset of implementations

```bash
python3 interop_runner.py --matrix \
    --implementations quicx,quiche,ngtcp2,quic-go \
    --no-sim --use-local-bin
```

### 4.3 A single scenario

```bash
# In matrix mode, only run v2
python3 interop_runner.py --matrix --implementations all \
    --scenario v2 --no-sim --use-local-bin

# Non-matrix, only run handshake (defaults to QuicX ↔ QuicX)
python3 interop_runner.py --scenario handshake --no-sim --use-local-bin
```

### 4.4 A single (server, client) pair

```bash
# QuicX server ↔ ngtcp2 client
python3 interop_runner.py --server quicx --client ngtcp2 \
    --no-sim --use-local-bin

# ngtcp2 server ↔ QuicX client
python3 interop_runner.py --server ngtcp2 --client quicx \
    --no-sim --use-local-bin
```

### 4.5 QuicX self-test

```bash
# Pure local processes (no Docker)
python3 interop_runner.py --local

# Docker mode, QuicX ↔ QuicX
python3 interop_runner.py --no-sim --use-local-bin
```

### 4.6 Persisting results to a file

```bash
# Markdown
python3 interop_runner.py --matrix --implementations all \
    --no-sim --use-local-bin \
    --output markdown --output-file logs/latest_matrix.md

# JSON
python3 interop_runner.py --matrix --implementations all \
    --no-sim --use-local-bin \
    --output json --output-file logs/latest_matrix.json
```

---

## 5. Full CLI reference

| Flag | Default | Meaning |
|------|---------|---------|
| `--matrix` | off | Matrix mode |
| `--full-matrix` | off | In matrix mode, also run third-party ↔ third-party |
| `--implementations LIST` | `quicx` | Comma-separated names, or `all` |
| `--server NAME` | quicx | Server implementation in non-matrix mode |
| `--client NAME` | quicx | Client implementation in non-matrix mode |
| `--scenario NAME` | all 14 | Run only the named scenario |
| `--no-sim` | off | Skip ns-3 simulation |
| `--use-local-bin` | off | Have QuicX use the host-built binaries |
| `--local` | off | Pure local-process mode (no Docker) |
| `--build-dir PATH` | `./build` | Build dir used by `--use-local-bin` |
| `--rebuild` | off | Force-rebuild the QuicX image |
| `--timeout SEC` | 60 | Per-test timeout |
| `--host HOST` | localhost | Client target host |
| `--port N` | 443 (4433 locally) | Server listen port |
| `--output FMT` | text | `text` / `markdown` / `json` |
| `--output-file PATH` | none | Write the result to this file |
| `-v`, `--verbose` | off | Debug-level logging |

---

## 6. Scenario catalogue (14)

Defined in `test/interop/testcases.py`, aligned with the upstream
quic-interop-runner.

| Scenario | Description |
|----------|-------------|
| `handshake` | Basic handshake, downloads 1 KB |
| `transfer` | Large-file transfer (1 MB + 5 MB) |
| `retry` | Server forces stateless retry |
| `resumption` | 1-RTT session resumption (two connections) |
| `zerortt` | 0-RTT early data |
| `http3` | HTTP/3, downloads 10 KB + 100 KB + 1 MB |
| `multiconnect` | Five concurrent client connections |
| `versionnegotiation` | Version negotiation (client offers an unsupported version) |
| `chacha20` | Forces ChaCha20-Poly1305 |
| `keyupdate` | Client triggers key update |
| `v2` | QUIC v2 (RFC 9369), version `0x6b3343cf` |
| `rebind-port` | Client NAT port rebinding |
| `rebind-addr` | Client NAT address rebinding |
| `connectionmigration` | Active client-driven connection migration |

### Verdict semantics

| Verdict | Meaning |
|---------|---------|
| `PASSED` | Client exits 0 and the downloaded file matches byte-by-byte |
| `FAILED` | Process error / hash mismatch / timeout |
| `UNSUPPORTED` | One side declared unsupported (exit code 127), or the current network mode cannot drive the scenario (e.g. `rebind-*` needs ns-3) |
| `SKIPPED` | The implementation declared itself as not playing this role in `implementations.json` |

---

## 7. Integrated peers

See `test/interop/implementations.json`.

| Implementation | Image | Roles |
|----------------|-------|-------|
| quicx    | `quicx-interop:latest` (built locally) | both |
| quiche   | `cloudflare/quiche-qns:latest` | both |
| ngtcp2   | `ghcr.io/ngtcp2/ngtcp2-interop:latest` | both |
| quic-go  | `martenseemann/quic-go-interop:latest` | both |
| mvfst    | `ghcr.io/facebook/proxygen/mvfst-interop:latest` | both |
| quinn    | `stammw/quinn-interop:latest` | both |
| aioquic  | `aiortc/aioquic-qns:latest` | both |
| picoquic | `privateoctopus/picoquic:latest` | both |
| neqo     | `ghcr.io/mozilla/neqo-qns:latest` | both |
| lsquic   | `litespeedtech/lsquic-qir:latest` | both |
| msquic   | `ghcr.io/microsoft/msquic/qns:main` | both |
| s2n-quic | `ghcr.io/aws/s2n-quic/s2n-quic-qns:latest` | both |

---

## 8. Log directory layout

```
test/interop/logs/
├── handshake/
│   ├── quicx_ngtcp2/                 # server=quicx, client=ngtcp2
│   │   ├── server/
│   │   │   ├── log.txt
│   │   │   ├── container_stdout.log
│   │   │   ├── container_stderr.log
│   │   │   └── qlog/*.qlog
│   │   └── client/
│   │       ├── container_stdout.log
│   │       ├── container_stderr.log
│   │       └── qlog/*.qlog
│   └── ngtcp2_quicx/...
├── transfer/...
├── v2/...
└── ...
```

Triage flow:
1. Look at `container_stderr.log` for stack traces.
2. Look at `log.txt` (QuicX business log) to see what handshake / transfer
   stage was reached.
3. Load `qlog/*.qlog` into [qvis](https://qvis.quictools.info/) for
   visualization.
4. If you need a packet capture, decrypt the UDP flow in Wireshark using
   `keys.log`.

---

## 9. Reproducing a single result

```bash
# Example: reproduce v2 with QuicX server and ngtcp2 client
python3 interop_runner.py --scenario v2 \
    --server quicx --client ngtcp2 \
    --no-sim --use-local-bin -v
```

`-v` enables debug logging, which prints the full container command, mounts
and environment variables for every test.

---

## 10. Related documents

- [`reports/interop_status.md`](../reports/interop_status.md) — current connectivity matrix
- [`guide/interop_overview.md`](./interop_overview.md) — official interop-runner internals
- [`../../internal/quic_interop_sim_issues.md`](../../internal/quic_interop_sim_issues.md) — per-peer triage notes
- `test/interop/INTEROP_improvement_plan.md` — improvement roadmap
- `test/interop/testcases.py` — the 14 scenario definitions
- `test/interop/implementations.json` — integrated peers
- `test/interop/run_endpoint.sh` — in-container QuicX startup script (scenario allow-list)
