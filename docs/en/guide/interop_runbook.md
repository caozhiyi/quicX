# QuicX Interop Test Usage

This document describes **how to run** QuicX's interop tests.

Status & result matrix lives in [`reports/interop_status.md`](../reports/interop_status.md).

Framework internals & official spec live in [`guide/interop_overview.md`](./interop_overview.md).

---

## 1. Overall approach

Interop testing is executed **directly with the official
[quic-interop-runner](https://github.com/quic-interop/quic-interop-runner)**.

The QuicX repo only provides the artifacts the official runner needs; the
in-repo runner has been retired:

```
test/interop/
├── CMakeLists.txt      # build rules for the interop binaries
├── interop_server.cpp  # interop server source
├── interop_client.cpp  # interop client source
├── Dockerfile          # builds the quicx-interop image
└── run_endpoint.sh     # in-container entrypoint (implements the runner's env-var protocol)
```

---

## 2. Building the QuicX Docker image

The official runner schedules implementations **by image name**, so build the
image from the QuicX repo root first:

```bash
docker build -t quicx-interop:latest -f test/interop/Dockerfile .
```

Notes:

- The image is based on the official
  `martenseemann/quic-network-simulator-endpoint`, with `/run_endpoint.sh` as
  the `ENTRYPOINT`, fully following the
  [quic-network-simulator](https://github.com/quic-interop/quic-network-simulator)
  endpoint conventions
- `interop_server` / `interop_client` are compiled from source inside the
  image; no pre-built local binaries are needed
- After a C++ change, **rebuild the image** before rerunning (the runner picks
  up whatever local image carries the `:latest` tag)

To publish to a registry (e.g. GHCR):

```bash
docker tag quicx-interop:latest ghcr.io/<owner>/quicx-interop:latest
docker push ghcr.io/<owner>/quicx-interop:latest
```

---

## 3. Setting up the official runner

```bash
git clone https://github.com/quic-interop/quic-interop-runner
cd quic-interop-runner
pip3 install -r requirements.txt
```

### 3.1 Requirements

| Requirement | Notes |
|-------------|-------|
| Docker + docker compose | Brings up the client / server / sim containers |
| Python 3 | Runs the runner |
| Wireshark **4.5.0+** | The runner uses tshark on sim pcaps to decide results (resumption / zerortt / keyupdate …) |
| Linux host | Run `sudo modprobe ip6table_filter` before IPv6 test cases |

### 3.2 Registering quicx

Make sure `implementations_quic.json` contains the quicx entry (add it if
missing):

```json
"quicx": {
  "image": "quicx-interop:latest",
  "url": "https://github.com/caozhiyi/quicX",
  "role": "both"
}
```

`role: both` means quicx participates in the matrix both as server and as
client.

---

## 4. Standard test commands

Run everything from the official runner's repo root.

### 4.1 QuicX as client

```bash
# Against a specific set of servers
python3 run.py -c quicx -s quiche,ngtcp2,quic-go

# Against every server
python3 run.py -c quicx
```

### 4.2 QuicX as server

```bash
# Against a specific set of clients
python3 run.py -s quicx -c ngtcp2,picoquic,aioquic

# Against every client
python3 run.py -s quicx
```

### 4.3 Selecting scenarios / reproducing a single result

```bash
# Only handshake, transfer and v2
python3 run.py -s quicx -c ngtcp2 -t handshake,transfer,v2

# Reproduce one failure with debug logs
python3 run.py -d -s quicx -c ngtcp2 -t v2
```

### 4.4 Full matrix

```bash
# Every server × every client × every scenario (very slow; use sparingly)
python3 run.py
```

### 4.5 Common flags

| Flag | Meaning |
|------|---------|
| `-s LIST` | server implementations (comma-separated) |
| `-c LIST` | client implementations (comma-separated) |
| `-t LIST` | test cases (comma-separated) |
| `-d` | debug logging |
| `-j FILE` | write the result matrix as JSON |
| `-m` | write the result matrix as Markdown |
| `-l DIR` | log directory (default `logs/`) |
| `-f` | save downloaded files when a test fails, for diffing |
| `-p PROTOCOL` | `quic` (default) / `webtransport` |

See `python3 run.py --help` for the full list.

---

## 5. Test scenarios

The official runner currently defines **22 conformance scenarios plus 2
measurement scenarios**. QuicX declares its supported set via the allow-list in
`run_endpoint.sh`; unsupported scenarios exit with code **127** per the
official convention and are recorded as `UNSUPPORTED`.

| Scenario | Description | QuicX |
|----------|-------------|-------|
| `handshake` | Basic handshake, small download | ✅ |
| `transfer` | Large-file transfer | ✅ |
| `retry` | Server forces stateless retry | ✅ |
| `resumption` | 1-RTT session resumption (two connections) | ✅ |
| `zerortt` | 0-RTT early data | ✅ |
| `http3` | HTTP/3 interaction | ✅ |
| `chacha20` | Forces ChaCha20-Poly1305 | ✅ |
| `keyupdate` | Client triggers key update | ✅ |
| `v2` | QUIC v2 (RFC 9369), version `0x6b3343cf` | ✅ |
| `rebind-port` | Client NAT port rebinding | ✅ |
| `rebind-addr` | Client NAT address rebinding | ✅ |
| `connectionmigration` | Active client-driven connection migration | ✅ |
| `ecn` | ECN marking and echo | ✅ |
| `longrtt` | High-RTT path | ❌ |
| `multiplexing` | Many concurrent streams | ❌ |
| `blackhole` | Transient network blackhole | ❌ |
| `amplificationlimit` | Amplification limit | ❌ |
| `handshakeloss` / `transferloss` | Loss during handshake / transfer | ❌ |
| `handshakecorruption` / `transfercorruption` | Corrupted packets during handshake / transfer | ❌ |
| `ipv6` | IPv6 connectivity | ❌ |
| `goodput` / `crosstraffic` | Throughput / cross-traffic measurement | ❌ |

> The `run_endpoint.sh` allow-list additionally contains `multiconnect` /
> `versionnegotiation` — scenarios from the retired in-repo runner that do not
> exist in the official matrix; they are kept for compatibility only.
> Per-scenario pass rates and failure root causes live in
> [`reports/interop_status.md`](../reports/interop_status.md).

---

## 6. Logs and triage

After each run, logs are saved under `logs/` in the runner directory
(override with `-l`):

```
logs/
└── <server>_<client>/          # e.g. quicx_ngtcp2
    └── <testcase>/             # e.g. v2
        ├── output.txt          # runner console output (incl. failure reason)
        ├── server/             # server-side logs (stdout/stderr, qlog)
        ├── client/             # client-side logs (stdout/stderr, qlog)
        └── sim/                # pcaps recorded by the simulator
```

Triage flow:

1. Check `output.txt` for the failure reason (timeout / file mismatch / exit 127)
2. Check QuicX's logs under `client/` or `server/` to see whether the failure
   is at the handshake or the transfer stage
3. Load `qlog` files into [qvis](https://qvis.quictools.info/) for
   visualization
4. Decrypt `sim/` pcaps in Wireshark using the `SSLKEYLOGFILE` (NSS Key Log
   format)

---

## 7. Related documents

- [`reports/interop_status.md`](../reports/interop_status.md) — current connectivity matrix
- [`guide/interop_overview.md`](./interop_overview.md) — official interop-runner internals
- [`../../internal/quic_interop_sim_issues.md`](../../internal/quic_interop_sim_issues.md) — per-peer triage notes
- `docs/internal/improvement_plan.md` — cross-cutting improvement plan (incl. interop)
- `test/interop/run_endpoint.sh` — in-container QuicX startup script (scenario allow-list)
- [quic-interop-runner](https://github.com/quic-interop/quic-interop-runner) — the official runner repository
