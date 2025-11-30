#!/bin/bash
set -e

# quic-interop-runner endpoint script for quicX
# This script is called by the interop runner with different test scenarios
#
# Environment variables set by interop runner:
# - ROLE: "client" or "server"
# - TESTCASE: test scenario name (e.g., "handshake", "transfer", "retry", etc.)
# - SSLKEYLOGFILE: path to write TLS secrets for Wireshark decryption
# - QLOGDIR: path to write qlog files
# - SERVER: server hostname (for client mode)
# - PORT: server port (for client mode)
# - REQUESTS: URLs to download (for client mode)
# - WWW: directory containing files to serve (for server mode)

QUICX_BIN="/quicx/build/bin"
ROLE="${ROLE:-server}"
TESTCASE="${TESTCASE:-handshake}"
PORT="${PORT:-443}"
WWW="${WWW:-/www}"
DOWNLOADS="${DOWNLOADS:-/downloads}"
LOGS="${LOGS:-/logs}"

# Generate self-signed certificate for server
generate_cert() {
    if [ ! -f /certs/cert.pem ]; then
        mkdir -p /certs
        openssl req -x509 -newkey rsa:2048 -nodes \
            -keyout /certs/key.pem \
            -out /certs/cert.pem \
            -days 365 \
            -subj "/CN=test.example.com"
    fi
}

# Run as server
run_server() {
    echo "Starting quicX server on port ${PORT}"
    echo "Test case: ${TESTCASE}"
    echo "WWW directory: ${WWW}"

    generate_cert

    # Build server command based on test case
    CMD="${QUICX_BIN}/interop_server"
    CMD+=" --port ${PORT}"
    CMD+=" --cert /certs/cert.pem"
    CMD+=" --key /certs/key.pem"
    CMD+=" --root ${WWW}"

    # Add test-specific flags
    case "${TESTCASE}" in
        versionnegotiation)
            # Server should reject unsupported versions
            CMD+=" --enforce-version"
            ;;
        retry)
            # Server should send Retry packet
            CMD+=" --force-retry"
            ;;
        resumption)
            # Server should support session resumption
            CMD+=" --enable-resumption"
            ;;
        zerortt)
            # Server should accept 0-RTT data
            CMD+=" --enable-0rtt"
            ;;
        multiconnect)
            # Multiple connections test
            CMD+=" --max-connections 100"
            ;;
        *)
            # Default server behavior
            ;;
    esac

    # Enable QLOG if requested
    if [ -n "${QLOGDIR}" ]; then
        CMD+=" --qlog-dir ${QLOGDIR}"
    fi

    echo "Command: ${CMD}"
    exec ${CMD}
}

# Run as client
run_client() {
    echo "Starting quicX client"
    echo "Test case: ${TESTCASE}"
    echo "Server: ${SERVER}:${PORT}"
    echo "Requests: ${REQUESTS}"

    # Build client command
    CMD="${QUICX_BIN}/interop_client"
    CMD+=" --server ${SERVER}"
    CMD+=" --port ${PORT}"
    CMD+=" --download-dir ${DOWNLOADS}"

    # Add test-specific flags
    case "${TESTCASE}" in
        versionnegotiation)
            # Client requests unsupported version
            CMD+=" --force-version 0x1a2a3a4a"
            ;;
        retry)
            # Client should handle Retry packet
            CMD+=" --enable-retry"
            ;;
        resumption)
            # Client should resume previous session
            CMD+=" --enable-resumption"
            ;;
        zerortt)
            # Client should send 0-RTT data
            CMD+=" --enable-0rtt"
            ;;
        http3)
            # HTTP/3 test
            CMD+=" --http3"
            ;;
        multiconnect)
            # Multiple connections
            CMD+=" --connections 100"
            ;;
        *)
            # Default client behavior
            ;;
    esac

    # Enable QLOG if requested
    if [ -n "${QLOGDIR}" ]; then
        CMD+=" --qlog-dir ${QLOGDIR}"
    fi

    # Add URLs to download
    for url in ${REQUESTS}; do
        CMD+=" ${url}"
    done

    echo "Command: ${CMD}"
    exec ${CMD}
}

# Main
case "${ROLE}" in
    server)
        run_server
        ;;
    client)
        run_client
        ;;
    *)
        echo "Unknown role: ${ROLE}"
        echo "Must be 'server' or 'client'"
        exit 1
        ;;
esac
