#!/usr/bin/env bash
# Convenience launcher for the HTTP/3 static server example.
#
# This single binary starts:
#   - HTTP/3 server on UDP --h3-port  (default 7010)  -- serves real files
#   - TCP upgrade endpoint on --http-port and/or --https-port
#       (default 8080 / 8443) -- speaks HTTP/1.1 and HTTP/2 just enough to
#       advertise `Alt-Svc: h3=":<h3-port>"` so a browser jumps to H3.
#
# What this script does:
#   1. Generates a self-signed cert (with SANs covering the host you specify)
#      only when missing, expired, or when SANs don't already cover the host.
#   2. Ensures the document root exists with a sample index.html.
#   3. Locates the compiled `static_server` binary across common build dirs.
#   4. Launches the server with the right CLI flags.
#
# Usage:
#   ./run.sh                                    # defaults
#   ./run.sh -H 9.134.40.248                    # bind cert SAN to this IP
#   ./run.sh -H my.host.com --h3-port 8443      # custom h3 port
#   ./run.sh --http-port 0                      # disable plaintext H1 endpoint
#   ./run.sh --no-upgrade                       # H3 only, no TCP listener
#   ./run.sh -r /var/www                        # custom doc root
#   ./run.sh -b /path/to/build                  # custom build dir
#   ./run.sh --regen-cert                       # force regenerate cert
#   ./run.sh -h                                 # help

set -euo pipefail

# ---- Defaults ---------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

HOST="localhost"
H3_PORT=17010
HTTP_PORT=18080
HTTPS_PORT=18443
NO_UPGRADE=0
DOC_ROOT="${SCRIPT_DIR}/www"
CERT_FILE="${SCRIPT_DIR}/cert.pem"
KEY_FILE="${SCRIPT_DIR}/key.pem"
BUILD_DIR=""
REGEN_CERT=0

# ---- Args -------------------------------------------------------------------
print_help() {
    sed -n '2,30p' "$0"
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -H|--host)        HOST="$2"; shift 2 ;;
        --h3-port)        H3_PORT="$2"; shift 2 ;;
        --http-port)      HTTP_PORT="$2"; shift 2 ;;
        --https-port)     HTTPS_PORT="$2"; shift 2 ;;
        --no-upgrade)     NO_UPGRADE=1; shift ;;
        -r|--doc-root)    DOC_ROOT="$2"; shift 2 ;;
        -c|--cert)        CERT_FILE="$2"; shift 2 ;;
        -k|--key)         KEY_FILE="$2"; shift 2 ;;
        -b|--build-dir)   BUILD_DIR="$2"; shift 2 ;;
        --regen-cert)     REGEN_CERT=1; shift ;;
        -h|--help)        print_help ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done

# Decide whether HOST is an IPv4 literal -> goes into IP SAN, else DNS SAN.
is_ipv4() {
    [[ "$1" =~ ^([0-9]{1,3}\.){3}[0-9]{1,3}$ ]]
}

# Build SAN list: always include localhost + 127.0.0.1, plus the user host.
build_san() {
    local extra_dns="" extra_ip=""
    if is_ipv4 "$HOST"; then
        extra_ip="$HOST"
    else
        extra_dns="$HOST"
    fi
    local san="DNS:localhost,IP:127.0.0.1,IP:::1"
    [[ -n "$extra_dns" && "$extra_dns" != "localhost" ]] && san="$san,DNS:$extra_dns"
    [[ -n "$extra_ip"  && "$extra_ip"  != "127.0.0.1" ]] && san="$san,IP:$extra_ip"
    echo "$san"
}

# ---- 1. Cert ----------------------------------------------------------------
need_regen=$REGEN_CERT
if [[ ! -f "$CERT_FILE" || ! -f "$KEY_FILE" ]]; then
    need_regen=1
elif ! openssl x509 -in "$CERT_FILE" -checkend 0 -noout >/dev/null 2>&1; then
    echo "[run.sh] Existing cert has expired, regenerating..."
    need_regen=1
else
    san_in_cert="$(openssl x509 -in "$CERT_FILE" -noout -ext subjectAltName 2>/dev/null || true)"
    target="$HOST"
    if is_ipv4 "$HOST"; then
        if ! grep -qE "IP Address:${target}( |$|,)" <<<"$san_in_cert"; then
            echo "[run.sh] Cert SANs do not cover IP $target, regenerating..."
            need_regen=1
        fi
    else
        if ! grep -qE "DNS:${target}( |$|,)" <<<"$san_in_cert"; then
            echo "[run.sh] Cert SANs do not cover DNS $target, regenerating..."
            need_regen=1
        fi
    fi
fi

if [[ "$need_regen" -eq 1 ]]; then
    if ! command -v openssl >/dev/null 2>&1; then
        echo "[run.sh] ERROR: openssl not found in PATH" >&2
        exit 1
    fi
    SAN="$(build_san)"
    CN="$HOST"
    echo "[run.sh] Generating self-signed cert (CN=$CN, SAN=$SAN, 365 days)..."
    openssl req -x509 -newkey rsa:2048 -nodes -days 365 \
        -keyout "$KEY_FILE" -out "$CERT_FILE" \
        -subj "/CN=$CN" \
        -addext "subjectAltName=$SAN" \
        >/dev/null 2>&1
    chmod 600 "$KEY_FILE"
    echo "[run.sh]   cert: $CERT_FILE"
    echo "[run.sh]   key : $KEY_FILE"
fi

# Compute SHA-256 SPKI fingerprint Chrome expects for
# --ignore-certificate-errors-spki-list (more targeted than the global flag).
SPKI_PIN="$(openssl x509 -in "$CERT_FILE" -pubkey -noout 2>/dev/null \
            | openssl pkey -pubin -outform der 2>/dev/null \
            | openssl dgst -sha256 -binary 2>/dev/null \
            | openssl enc -base64 2>/dev/null || true)"

# ---- 2. Doc root ------------------------------------------------------------
mkdir -p "$DOC_ROOT"
if [[ ! -f "$DOC_ROOT/index.html" ]]; then
    cat > "$DOC_ROOT/index.html" <<HTML
<!doctype html>
<meta charset="utf-8">
<title>quicX HTTP/3 static</title>
<style>
  body { font-family: -apple-system, system-ui, sans-serif; margin: 4em auto; max-width: 40em; }
  code { background: #f4f4f4; padding: 0.1em 0.4em; border-radius: 3px; }
</style>
<h1>It works over HTTP/3</h1>
<p>Served by <code>quicX</code> from <code>$HOST</code>.</p>
<p>Open DevTools &rarr; Network &rarr; enable the <strong>Protocol</strong> column.
   You should see <code>h3</code>.</p>
HTML
    echo "[run.sh] Wrote sample $DOC_ROOT/index.html"
fi

# ---- 3. Locate binary -------------------------------------------------------
find_binary() {
    local candidates=()
    # 1) Highest priority: explicit --build-dir.
    #    Search common output layouts inside it:
    #      <build>/bin/static_server                 (RUNTIME_OUTPUT_DIRECTORY=bin)
    #      <build>/example/static_server/static_server  (default per-target dir)
    #      <build>/static_server                     (flat)
    if [[ -n "$BUILD_DIR" ]]; then
        candidates+=(
            "$BUILD_DIR/bin/static_server"
            "$BUILD_DIR/example/static_server/static_server"
            "$BUILD_DIR/static_server"
        )
    fi
    # 2) Common in-repo build dirs.
    for bd in build build-gcc-debug build-asan cmake-build-debug cmake-build-release; do
        candidates+=(
            "$REPO_ROOT/$bd/bin/static_server"
            "$REPO_ROOT/$bd/example/static_server/static_server"
            "$REPO_ROOT/$bd/static_server"
        )
    done
    for c in "${candidates[@]}"; do
        if [[ -x "$c" ]]; then echo "$c"; return 0; fi
    done
    return 1
}

if ! BIN="$(find_binary)"; then
    echo "[run.sh] ERROR: static_server binary not found." >&2
    echo "[run.sh] Build it first, e.g.:" >&2
    echo "    cd $REPO_ROOT/build && cmake --build . --target static_server -j" >&2
    exit 1
fi

# ---- 4. Launch --------------------------------------------------------------
ARGS=(
    --doc-root  "$DOC_ROOT"
    --cert      "$CERT_FILE"
    --key       "$KEY_FILE"
    --host      "0.0.0.0"
    --h3-port   "$H3_PORT"
    --http-port "$HTTP_PORT"
    --https-port "$HTTPS_PORT"
)
[[ "$NO_UPGRADE" -eq 1 ]] && ARGS+=(--no-upgrade)

cat <<EOF
[run.sh] Starting static_server (single process)
         binary    : $BIN
         host      : $HOST   (bind 0.0.0.0)
         h3 port   : $H3_PORT  (UDP, real file serving)
         http port : $HTTP_PORT  (TCP, upgrade only, 0=disabled)
         https port: $HTTPS_PORT  (TCP, upgrade only, 0=disabled)
         doc_root  : $DOC_ROOT
         cert      : $CERT_FILE
         key       : $KEY_FILE
EOF

if [[ "$NO_UPGRADE" -eq 0 && "$HTTPS_PORT" -ne 0 ]]; then
cat <<EOF

Recommended browser flow (one-time TCP, then auto-upgrade to H3):

  1) Visit https://$HOST:$HTTPS_PORT/  ->  picks up Alt-Svc -> next request uses h3
  2) Or skip TCP entirely with --origin-to-force-quic-on:

     google-chrome \\
       --user-data-dir=/tmp/chrome-h3 \\
       --origin-to-force-quic-on=$HOST:$H3_PORT \\
       --ignore-certificate-errors \\
       https://$HOST:$H3_PORT/

EOF
else
cat <<EOF

Open in Chrome (force H3, since no TCP upgrade endpoint):

  google-chrome \\
    --user-data-dir=/tmp/chrome-h3 \\
    --origin-to-force-quic-on=$HOST:$H3_PORT \\
    --ignore-certificate-errors \\
    https://$HOST:$H3_PORT/

EOF
fi

if [[ -n "$SPKI_PIN" ]]; then
cat <<EOF
For a more targeted alternative to --ignore-certificate-errors, pin the SPKI:

  --ignore-certificate-errors-spki-list=$SPKI_PIN

EOF
fi

cat <<EOF
Test via curl (HTTP/3-capable build required):

  curl --http3-only -k https://$HOST:$H3_PORT/

Press Ctrl+C to stop.

EOF

exec "$BIN" "${ARGS[@]}"
