# quicx_curl — HTTP/3 Command-Line Client

A curl-style command-line tool built on the [quicx](../../README.md) library:

- **Full coverage of high-frequency curl scenarios**: GET/POST/PUT/HEAD, custom headers, Basic Auth, redirects, cookies, timeouts, `-o/-O/-w/-f` output control, and more;
- **HTTP/3 & QUIC feature set**: qlog traces, TLS key export (Wireshark decryption), 0-RTT session resumption, QUIC v1/v2, ECN, Key Update, connection migration demo, Server Push display;
- **Streaming transfer**: the response body is written as it arrives (no full-buffering) — verified with a 5 MB random file download whose md5 matches the source, at ~57 MB/s throughput.

```
Usage: quicx_curl [options] <URL> [URL...]
HTTP/3 command-line tool powered by quicx (curl-like + QUIC features)
```

## Build

```bash
# from the repository root
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc) --target quicx_curl
# binary: build/bin/quicx_curl
```

Dependencies: C++17, OpenSSL. The QUIC/HTTP3 implementation lives in the `http3` target and is linked automatically by CMake.

## Quick Start

```bash
# basic request (note: TLS certificate verification is on by default, same as curl)
quicx_curl https://example.com/

# local server with a self-signed certificate (example servers all use self-signed certs)
quicx_curl -k https://127.0.0.1:5000/

# headers only
quicx_curl -k -I https://127.0.0.1:5000/

# download to file + metrics
quicx_curl -k -s -o page.html -w 'code=%{http_code} size=%{size_download} ttfb=%{time_starttransfer}s\n' \
    https://127.0.0.1:5000/
```

Only the `https` URL scheme is supported (HTTP/3 semantics); non-https URLs exit with code 1.

## High-Frequency curl Scenarios

### Request Construction

| Option | Description |
|--------|-------------|
| `-X, --request <method>` | HTTP method (GET/POST/PUT/DELETE/...) |
| `-H, --header <header>` | Append a request header `"Name: value"`; repeatable |
| `-d, --data <data\|@file>` | Request body; `@file` reads from a file; **automatically switches GET to POST** (curl semantics) |
| `-T, --upload-file <file>` | Streaming upload (body provider feeds in chunks, no full in-memory read); **defaults to PUT when `-X` is not given** |
| `-I, --head` | HEAD method; implies showing response headers (curl semantics) |
| `-A, --user-agent <name>` | User-Agent header |
| `-e, --referer <url>` | Referer header |
| `-u, --user <user:pass>` | Basic Auth (Base64-encoded and sent as the Authorization header) |

```bash
# POST JSON
quicx_curl -k -X POST -H 'Content-Type: application/json' \
    -d '{"name": "quicx"}' https://127.0.0.1:5000/api/echo

# read the request body from a file
quicx_curl -k -d @payload.bin https://127.0.0.1:5000/upload

# stream-upload a 1GB file without memory pressure (implies PUT)
quicx_curl -k -T bigfile.bin https://127.0.0.1:7006/upload/bigfile
```

### Redirects and Cookies

| Option | Description |
|--------|-------------|
| `-L, --location` | Follow 3xx redirects (301/302/303 switch POST to GET; 307/308 keep the method) |
| `--max-redirs <num>` | Maximum hops, default 50; exceeding it stops following and returns the last 3xx response |
| `-b, --cookie <file>` | Send cookies from a Netscape-format cookie file (or inline `k=v; k2=v2`) |
| `-c, --cookie-jar <file>` | Write session Set-Cookie entries back to a Netscape jar after the request finishes |

```bash
# log in and keep the session
quicx_curl -k -c cookies.txt -d 'user=a&pass=b' https://127.0.0.1:5000/login
quicx_curl -k -b cookies.txt https://127.0.0.1:5000/profile
```

### Output Control

| Option | Description |
|--------|-------------|
| `-o, --output <file>` | Write body to a file; **a single `-o` applies to all URLs**, multiple `-o`s map to URLs in order (curl semantics), `-o -` writes to stdout |
| `-O, --remote-name` | Save under the filename from the tail of the URL |
| `-i, --include` | Include response headers in the output |
| `-f, --fail` | No body output on HTTP ≥ 400; exit code 22 |
| `-w, --write-out <fmt>` | Print metrics per the format string after the transfer (see table below) |
| `-v, --verbose` | Verbose diagnostics on stderr (tool's own `*` lines + library logs) |
| `-s, --silent` / `-S, --show-error` | Silent mode / still show errors when silent |
| `-#, --progress-bar` | Progress bar instead of the rate table |

```bash
# multiple URLs: two -o files map one-to-one
quicx_curl -k -o a.html -o b.html https://h1/ https://h2/

# one -o applies to all URLs; -w prints one line per URL
quicx_curl -k -s -o /dev/null -w '%{http_code} %{url}\n' https://h1/ https://h2/
```

### Timeouts

| Option | Description |
|--------|-------------|
| `--connect-timeout <sec>` | Connection-phase timeout (0 = library default) |
| `--max-time <sec>` | Whole-operation timeout (including the redirect chain); timeout exits with code 28 |
| `-k, --insecure` | Skip certificate verification (**verification is on by default**; a verification failure exits with code 7) |
| `--cacert <file>` | CA bundle used to verify the peer |

## HTTP/3 & QUIC Features

This group is the tool's core value beyond curl (with an HTTP/3 build).

### qlog Traces (RFC 9254)

```bash
quicx_curl -k --qlog ./qlogs https://127.0.0.1:5000/
# output: qlogs/<UTC-timestamp>_<PID>.sqlog
# format: JSON-SEQ (qlog_version 0.3), protocol_types = ["QUIC","HTTP3"]
```

Covers connection establishment, loss recovery, congestion control, flow control, path validation and other events — usable for offline analysis or [qvis](https://qvis.quictools.info/) visualization (qvis supports JSON-SEQ import).

### TLS Key Export (Wireshark Decryption)

```bash
quicx_curl -k --keylog ./keys.log https://127.0.0.1:5000/
# Wireshark → Preferences → Protocols → TLS → (Pre)-Master-Secret log filename
```

Exports the standard `SSLKEYLOGFILE` format (CLIENT_HANDSHAKE_TRAFFIC_SECRET lines etc.); combined with Wireshark it fully decrypts QUIC traffic.

### 0-RTT Session Resumption

```bash
# --0rtt implicitly enables the session cache (default: ./session_cache)
quicx_curl -k --0rtt https://127.0.0.1:5000/          # first run: full handshake, caches the session ticket
quicx_curl -k --0rtt -v https://127.0.0.1:5000/ 2>&1 | grep -i early
# early_data_capable=1 in the log means the server issued a ticket and later connections can send early data

# or specify the cache directory explicitly
quicx_curl -k --session-cache /tmp/sc --0rtt https://host/
```

> Note: whether 0-RTT actually sends early data depends on the server issuing a session ticket (the quicx example server does not enable tickets by default). The client-side session storage/reuse path is fully verified.

### QUIC Version / ECN / Key Update / Keep-Alive

```bash
quicx_curl -k --quic-version v2 https://host/     # RFC 9369 QUIC v2 (version-negotiation exercise)
quicx_curl -k --ecn https://host/                 # IP-layer ECN congestion feedback
quicx_curl -k --key-update https://host/          # automatic key update mid-connection (RFC 9001 §6)
quicx_curl -k --keep-alive 15000 https://host/    # QUIC PING every 15s to keep the NAT mapping alive
```

### Connection Migration Demo

```bash
# switch the local port mid-transfer to exercise PATH_NEW_CONNECTION_ID + path validation
quicx_curl -k -v -s -o big.bin \
    --migrate --migrate-delay 500 \
    https://127.0.0.1:7006/big.bin
# stderr sequence:
#   * initiating mid-transfer migration (delay elapsed)
#   * migration initiate result: accepted
#   (the migration-completion verdict callback depends on the library's path-validation timing)

# specify the migration target address (default = same IP, new port)
quicx_curl -k --migrate --migrate-to 192.168.1.20:0 https://host/big.bin
```

`--migrate-delay <ms>`: trigger N ms after the first byte arrives (0 = trigger after the request finishes). Handy with a large file download to observe "the transfer never stalls while the underlying 4-tuple changes".

### Server Push

```bash
quicx_curl -k --push -v https://127.0.0.1:7008/hello
# stderr:
#   * push promise: https://127.0.0.1:7008/hello — accepted
#   * push response: HTTP/3 200, 11 bytes
```

Pushed content is summarized on stderr (status + byte count) and **never mixed into the main response body channel**, so data written by `-o` contains only the main request's response.

### QUIC Statistics

```bash
quicx_curl -k -s -o /dev/null --stats https://127.0.0.1:7006/big.bin
# ===== transfer stats =====
# http status:              200
# redirect hops:            0
# time_starttransfer:       0.012 s
# time_total:               0.094 s
# size_download:            5242880 bytes
# speed_download:           55562553.2 bytes/s
# quic_version:             v1
# 0-rtt:                    off
# quic_rtt:                 N/A (not exported by stack)
# quic_loss:                N/A (not exported by stack)
# quic_cwnd:                N/A (not exported by stack)
# ===========================
```

Aggregates phase timings and throughput to stderr; `rtt/loss/cwnd` will be wired up once the library-side statistics interface is ready (currently reported honestly as N/A — no fabricated data), for performance observation.

## `-w` write-out Variables

In the format string, `%{name}` expands to a metric; `\n` `\t` `\r` escapes are supported; unknown variables are left as-is.

| Variable | Meaning |
|----------|---------|
| `url` / `url_effective` | original URL / URL at the end of the redirect chain |
| `http_code` / `response_code` | final response status code (0 when no response) |
| `size_download` / `size_upload` | bytes downloaded / uploaded |
| `time_total` | total time (seconds, 3 decimals) |
| `time_starttransfer` | time to first byte (TTFB) |
| `speed_download` | download speed (B/s) |
| `num_redirects` | number of redirects actually followed |
| `content_type` | response Content-Type |
| `errormsg` | error description (empty on success) |
| `quic_version` | negotiated QUIC version (`N/A` when not reported) |
| `quic_rtt` / `quic_0rtt` / `quic_loss` | reserved; currently output `N/A` |

## Exit Codes

Consistent with curl:

| Code | Meaning |
|------|---------|
| 0 | success |
| 1 | unsupported protocol (non-https URL) |
| 3 | malformed URL |
| 7 | connection failed (including TLS handshake failure, certificate verification failure) |
| 22 | `-f` and HTTP ≥ 400 |
| 23 | write failure (`-o` target not writable, etc.) |
| 26 | read failure (`-d @file`/`-T` file not readable) |
| 28 | `--max-time` timeout |

## Differences from curl / Known Limitations

1. **https only**: QUIC mandates TLS; `http://` is explicitly rejected (exit code 1), never silently upgraded; no `--resolve`/proxy options;
2. **Connection-refused semantics**: QUIC runs over UDP, so an unlistened port does not fail immediately — you must wait for the Initial retry timeout (`--connect-timeout` recommended);
3. when `-d` inline body and `-T` conflict, `-T` wins; `-d` switches the default GET to POST and `-T` to PUT (curl defaults); an explicit `-X` takes precedence. Note that `example/file_transfer_server` only registers `POST /upload/*` — uploads need `-X POST -T`;
4. 0-RTT early data depends on the server issuing a session ticket;
5. the migration-completion callback (SUCCESS/FAILED verdict) depends on the library's path-validation timing; the initiation result (accepted) is always printed;
6. **multi-value headers follow the RFC**: the library stores headers as ordered field lines (RFC 9110 §5.3); multiple `Set-Cookie` entries are preserved intact into the cookie jar — no comma merging.

## Module Layout

```
example/quicx_curl/
├── main.cpp            # assembly: parse → validate → orchestrator.Run
├── cli/                # option table (data-driven: adding an option = adding a row) + three-part Options
├── app/                # orchestrator (redirect loop) / request_driver (streaming state machine) / builder
├── session/            # the single translation point for H3 switches (transport_map): qlog/keylog/0rtt/version/...
├── features/           # redirect / cookie_jar / migration_ctl / push_display (mutually independent)
├── io/                 # ResponseSink (stdout/file/discard) / writeout / progress bar
└── obs/                # Timing phases / --stats / verbose channel
```

Dependencies flow strictly downward: `main → app → {session, features, io, obs}`; business modules only read `Options`, never write back.

## Functional Tests

```bash
python3 example/quicx_curl/run_functional_test.py build/bin
```

Automatically spins up 4 example servers (`restful_api_server` :7007, `file_transfer_server` :7006,
`error_handling_server` :7005, `server_push` :7008) and covers 50 assertions: basic requests, all HTTP
methods, headers/auth, redirects, cookies, 30MB upload/download md5 integrity, output control, timeouts
and 8 exit-code classes, plus every H3 option (qlog/keylog/0-RTT/v2/ECN/Key-Update/Keep-Alive/migration/
push/stats). Exit code 0 means all green.

`restful_api_server` gained 4 test endpoints for this purpose: `/redirect` (302), `/redirect-loop` (302
self-referencing), `/set-cookie`, `/echo-headers`, plus `HEAD /users`.

## Measured Reference

The results below come from smoke runs against this repository's example servers (`hello_world_server`
:7001 / `file_transfer_server` :7006):

```text
$ quicx_curl -k -s -w 'HTTP=%{http_code} size=%{size_download} ttfb=%{time_starttransfer}s total=%{time_total}s speed=%{speed_download}B/s\n' \
      https://127.0.0.1:7001/metrics
HTTP=200 size=19738 ttfb=0.009s total=0.010s speed=1973800.0B/s

# 5 MB /dev/urandom random file downloaded via file_transfer_server, md5 matches the source (binary-safe)
# 1 MB random file uploaded with -T, server-side md5 matches the local file
# --max-time 0.05 interrupting a large download → exit 28
# default certificate verification failure (self-signed) → exit 7 (library handshake error correctly propagated, no hang)
```
