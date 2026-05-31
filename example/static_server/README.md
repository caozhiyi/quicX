# Static HTTP/3 file server (browser-accessible)

This example runs a **HTTP/3-only** static file server and shows how to make
Chrome talk to it.

## Why a "normal" HTTP/3 server is not directly accessible from the browser

Chrome (and Firefox/Edge) does **not** open an HTTP/3 connection out of the
blue. The standard discovery flow is:

1. Browser opens `https://host:port` over **TCP** (HTTP/1.1 or HTTP/2).
2. The server returns header `Alt-Svc: h3=":port"`.
3. The browser remembers it and on subsequent requests upgrades to HTTP/3
   (UDP).

Since `quicX` only speaks HTTP/3, we have two choices:

- **Force Chrome to use HTTP/3 directly** with the
  `--origin-to-force-quic-on=host:port` command-line flag. This is the
  recommended way for local testing.
- Run a small TCP HTTPS reverse-proxy in front whose only job is to advertise
  `Alt-Svc`. (Not covered here.)

In addition, Chrome verifies the TLS certificate. Self-signed/expired certs
cause QUIC to fail silently (no clickable "proceed anyway" prompt like TCP),
so we either install the cert as locally trusted, or pass
`--ignore-certificate-errors`.

## 1. Generate a self-signed cert for `localhost`

```bash
cd example/static_server
openssl req -x509 -newkey rsa:2048 -nodes -days 365 \
  -keyout key.pem -out cert.pem \
  -subj "/CN=localhost" \
  -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"
```

The cert hard-coded in `hello_world/server.cpp` is **expired (2014-2017)**
and has no `localhost` SAN — that's why browsers refuse it.

## 2. Build & run

```bash
# from the repo root, after running cmake:
./build/example/static_server/static_server ./www ./cert.pem ./key.pem
```

The binary will create `./www/index.html` automatically on first run if the
directory does not exist. Drop any static files (HTML/JS/CSS/images) into
`./www`.

## 3. Open in Chrome

Use a dedicated Chrome profile so the flags don't leak into your normal
browsing:

```bash
google-chrome \
  --user-data-dir=/tmp/chrome-h3 \
  --origin-to-force-quic-on=localhost:7010 \
  --ignore-certificate-errors \
  https://localhost:7010/
```

(Replace `google-chrome` with `chromium`, or on macOS:
`/Applications/Google\ Chrome.app/Contents/MacOS/Google\ Chrome`.)

Open DevTools → Network → enable the **Protocol** column. You should see
`h3` for the page load.

## Common failure modes

| Symptom in Chrome                        | Cause                                          |
| ---------------------------------------- | ---------------------------------------------- |
| `ERR_CONNECTION_REFUSED`                 | Server not started, or TCP port hit instead of UDP |
| `ERR_QUIC_PROTOCOL_ERROR`                | Cert/SNI invalid, or expired self-signed cert  |
| Loads as HTTP/1 forever                  | Forgot `--origin-to-force-quic-on`             |
| Hangs on first request                   | UDP blocked by firewall / NAT                  |
| `ERR_CERT_AUTHORITY_INVALID`             | Need `--ignore-certificate-errors` (dev only)  |

## Verifying without a browser

If you just want to confirm the server is up, use the bundled `quicx-curl`:

```bash
./build/example/quicx_curl/quicx-curl https://localhost:7010/index.html
```

or any other HTTP/3-capable curl:

```bash
curl --http3-only -k https://localhost:7010/index.html
```
