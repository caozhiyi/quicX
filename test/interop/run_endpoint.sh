#!/bin/bash
################################################################################
# quicX QUIC Interop Runner Endpoint Script
#
# Fully compatible with official quic-interop-runner framework
# Reference: https://github.com/quic-interop/quic-interop-runner
# Reference: https://github.com/quic-interop/quic-network-simulator
#
# Environment variables (set by interop runner):
#   ROLE          - "server" or "client"
#   TESTCASE      - test case name (e.g., handshake, transfer, v2)
#   SERVER_PARAMS - additional server parameters
#   CLIENT_PARAMS - additional client parameters
#   REQUESTS      - space-separated URLs for client to download
#   QLOGDIR       - directory for qlog output
#   SSLKEYLOGFILE - path for TLS key log
################################################################################

# NOTE: We do NOT use "set -e" here because /setup.sh (from the base image)
# runs multiple ip-route commands internally, and in direct-mode those will
# fail.  We handle errors explicitly where needed.

# Execute network setup (required by quic-network-simulator)
# /setup.sh is provided by martenseemann/quic-network-simulator-endpoint base image
# It configures routes so that traffic goes through the ns-3 simulator.
#
# ONLY run /setup.sh when the container is in the 172.30.x.x simulator
# topology.  In direct mode (e.g. 10.0.0.x bridge network) the script will
# try to configure IPv6 routes for addresses that don't exist, producing
# errors like "inet6 address is expected rather than ':2'" and potentially
# breaking the container's routing table.
if ip addr show 2>/dev/null | grep -qE "172\.30\.(0|100)\."; then
    echo "Network simulator topology detected (172.30.x.x), running /setup.sh ..."
    if [ -f /setup.sh ]; then
        /setup.sh || {
            echo "WARNING: /setup.sh failed (exit $?). Continuing anyway."
        }
    fi
else
    echo "Direct mode detected (not in 172.30.x.x network), skipping /setup.sh"
    # Ensure log directories exist (normally created by /setup.sh)
    mkdir -p /logs/qlog
fi

# Display version info
echo "quicX Interop Endpoint"
echo "======================"
echo "Git commit: $(cat /commit.txt 2>/dev/null || echo 'unknown')"
echo "Role: ${ROLE:-server}"
echo "Test case: ${TESTCASE:-handshake}"
echo ""

# Show network info for debugging
echo "Network interfaces:"
ip -brief addr show 2>/dev/null || ifconfig 2>/dev/null || true
echo ""

# Define supported and unsupported test cases
# Supported tests: must match scenarios in testcases.py that quicX can handle
# NOTE: "multiplexing" was removed — it is not a valid scenario in testcases.py.
#       "multiconnect" is the correct name for concurrent client testing.
SUPPORTED_TESTS="handshake transfer retry resumption zerortt multiconnect versionnegotiation chacha20 keyupdate v2 rebind-port rebind-addr connectionmigration http3"

# Explicitly unsupported tests (none currently)
UNSUPPORTED_TESTS=""

# Check if test case is supported
check_testcase_support() {
    local testcase=$1
    
    # Check if explicitly unsupported
    for unsupported in $UNSUPPORTED_TESTS; do
        if [ "$testcase" = "$unsupported" ]; then
            echo "ERROR: Test case '$testcase' is not supported by quicX"
            exit 127  # Official convention: unsupported tests exit with 127
        fi
    done
    
    # Check if in supported list
    local is_supported=0
    for supported in $SUPPORTED_TESTS; do
        if [ "$testcase" = "$supported" ]; then
            is_supported=1
            break
        fi
    done
    
    # If not in supported list, return UNSUPPORTED
    if [ $is_supported -eq 0 ]; then
        echo "ERROR: Test case '$testcase' is unknown/unsupported by quicX"
        echo "Supported tests: $SUPPORTED_TESTS"
        exit 127  # Official convention: unsupported tests exit with 127
    fi
}

# Check test case support
# Official interop-runner uses TESTCASE_CLIENT/TESTCASE_SERVER for compliance checks
# Use whichever is set (TESTCASE_CLIENT for client role, TESTCASE_SERVER for server role)
ACTUAL_TESTCASE="${TESTCASE_CLIENT:-${TESTCASE_SERVER:-${TESTCASE:-handshake}}}"
check_testcase_support "$ACTUAL_TESTCASE"

# Client mode
run_client() {
    echo "Starting quicX client..."
    echo "Server: ${SERVER}:${PORT:-443}"
    echo "Requests: ${REQUESTS}"
    echo "Downloads: ${DOWNLOADS:-/downloads}"
    
    # Wait for network simulator to be ready (required by quic-network-simulator)
    # /wait-for-it.sh is provided by the base image
    # Only wait when running inside the network simulator (check if sim is reachable)
    if ip addr show 2>/dev/null | grep -q "193\.167\."; then
        echo "Network simulator environment detected (custom bridge network), waiting for sim..."
        if [ -f /wait-for-it.sh ]; then
            # Wait for sim's control port (ns-3 readiness signal)
            if getent hosts sim > /dev/null 2>&1; then
                /wait-for-it.sh sim:57832 -s -t 30 || true
            fi
        fi
    else
        echo "Host network mode detected, skipping network simulator wait."
    fi
    
    # Build client command
    local cmd="/usr/local/bin/interop_client"
    # Note: Server hostname and port are automatically extracted from the request URLs
    cmd+=" --download-dir ${DOWNLOADS:-/downloads}"
    
    # Test case specific parameters
    case "$ACTUAL_TESTCASE" in
        versionnegotiation)
            cmd+=" --force-version 0x1a2a3a4a"
            ;;
        retry)
            cmd+=" --expect-retry"
            ;;
        resumption)
            cmd+=" --session-cache /tmp/session"
            cmd+=" --resumption"
            ;;
        zerortt)
            cmd+=" --session-cache /tmp/session"
            cmd+=" --zerortt"
            ;;
        chacha20)
            cmd+=" --cipher TLS_CHACHA20_POLY1305_SHA256"
            ;;
        keyupdate)
            cmd+=" --force-keyupdate"
            ;;
        v2)
            cmd+=" --quic-version 0x6b3343cf"
            ;;
        http3)
            cmd+=" --http3"
            ;;
    esac
    
    # Enable QLOG
    if [ -n "${QLOGDIR}" ]; then
        cmd+=" --qlog-dir ${QLOGDIR}"
    fi
    
    # Pass additional client parameters (from official runner)
    if [ -n "${CLIENT_PARAMS}" ]; then
        cmd+=" ${CLIENT_PARAMS}"
    fi
    
    # Add request URLs
    for url in ${REQUESTS}; do
        cmd+=" ${url}"
    done
    
    echo "Command: $cmd"
    exec $cmd
}

# Server mode
run_server() {
    echo "Starting quicX server..."
    echo "Port: ${PORT:-443}"
    echo "WWW: ${WWW:-/www}"
    
    # Build server command
    local cmd="/usr/local/bin/interop_server"
    cmd+=" --port ${PORT:-443}"
    cmd+=" --root ${WWW:-/www}"
    
    # Use official standard certificate paths (/certs is mounted by runner)
    cmd+=" --cert /certs/cert.pem"
    cmd+=" --key /certs/priv.key"
    
    # Test case specific parameters
    case "$ACTUAL_TESTCASE" in
        retry)
            cmd+=" --force-retry"
            ;;
        resumption|zerortt)
            cmd+=" --enable-resumption"
            cmd+=" --enable-0rtt"
            ;;
        chacha20)
            cmd+=" --cipher TLS_CHACHA20_POLY1305_SHA256"
            ;;
        keyupdate)
            cmd+=" --enable-keyupdate"
            ;;
        versionnegotiation)
            cmd+=" --strict-version"
            ;;
        v2)
            cmd+=" --quic-version 0x6b3343cf"
            ;;
        http3)
            cmd+=" --http3"
            ;;
    esac
    
    # Enable QLOG
    if [ -n "${QLOGDIR}" ]; then
        cmd+=" --qlog-dir ${QLOGDIR}"
    fi
    
    # Pass additional server parameters (from official runner)
    if [ -n "${SERVER_PARAMS}" ]; then
        cmd+=" ${SERVER_PARAMS}"
    fi
    
    echo "Command: $cmd"
    exec $cmd
}

# Main logic
case "${ROLE}" in
    client)
        run_client
        ;;
    server|"")
        run_server
        ;;
    *)
        echo "ERROR: Unknown role '${ROLE}'. Must be 'client' or 'server'."
        exit 1
        ;;
esac
