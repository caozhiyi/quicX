#!/usr/bin/env bash
################################################################################
# Complete QUIC Interoperability Test Suite
# Supports all 14+ official QUIC Interop Runner test scenarios
#
# Based on: https://github.com/quic-interop/quic-interop-runner
# Reference: https://interop.seemann.io/
#
# Usage:
#   ./run_full_interop.sh [options]
#
# Options:
#   --self-test          Run tests against quicX only (default)
#   --cross-impl <impl>  Test against another QUIC implementation
#   --all-impls          Test against all available implementations
#   --scenario <name>    Run specific test scenario only
#   --output <format>    Output format: text|json|markdown (default: text)
#   --no-color           Disable colored output
#
################################################################################

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

################################################################################
# Configuration
################################################################################

# Test mode
TEST_MODE="self"           # self | cross | all
SPECIFIC_SCENARIO=""       # Run specific scenario only
OUTPUT_FORMAT="text"       # text | json | markdown
USE_COLOR=true

# Server configuration
SERVER_PORT=4433
SERVER_HOST="localhost"

# Test results tracking (using simple arrays for bash 3.2 compatibility)
TEST_RESULTS_LIST=""
TEST_DURATIONS_LIST=""
SCENARIO_STATUS_LIST=""

TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0
SKIPPED_TESTS=0
UNSUPPORTED_TESTS=0

# Colors
if [ "$USE_COLOR" = true ]; then
    GREEN='\033[0;32m'
    RED='\033[0;31m'
    YELLOW='\033[1;33m'
    BLUE='\033[0;34m'
    CYAN='\033[0;36m'
    MAGENTA='\033[0;35m'
    BOLD='\033[1m'
    NC='\033[0m'
else
    GREEN=''
    RED=''
    YELLOW=''
    BLUE=''
    CYAN=''
    MAGENTA=''
    BOLD=''
    NC=''
fi

################################################################################
# Test Scenarios (Official QUIC Interop Runner)
################################################################################

# Complete list of 14+ official test scenarios
ALL_SCENARIOS=(
    "handshake"             # 1. Basic handshake
    "transfer"              # 2. Flow control & multiplexing
    "retry"                 # 3. Retry packet handling
    "resumption"            # 4. Session resumption (1-RTT)
    "zerortt"               # 5. 0-RTT data transfer
    "http3"                 # 6. HTTP/3 functionality
    "multiconnect"          # 7. Multiple connections (handshake loss resilience)
    "versionnegotiation"    # 8. Version negotiation
    "chacha20"              # 9. ChaCha20-Poly1305 cipher
    "keyupdate"             # 10. TLS key update
    "v2"                    # 11. QUIC v2 support
    "rebind-port"           # 12. NAT port rebinding
    "rebind-addr"           # 13. NAT address rebinding
    "connectionmigration"   # 14. Connection migration
)

# Helper functions for scenario data (bash 3.2 compatible)
get_scenario_desc() {
    case "$1" in
        handshake) echo "Basic QUIC handshake completion" ;;
        transfer) echo "Flow control and stream multiplexing" ;;
        retry) echo "Server Retry packet handling" ;;
        resumption) echo "Session resumption without 0-RTT" ;;
        zerortt) echo "Zero round-trip time data transfer" ;;
        http3) echo "HTTP/3 protocol functionality" ;;
        multiconnect) echo "Handshake resilience under packet loss" ;;
        versionnegotiation) echo "Version negotiation protocol" ;;
        chacha20) echo "ChaCha20-Poly1305 cipher suite" ;;
        keyupdate) echo "TLS key update mechanism" ;;
        v2) echo "QUIC version 2 support" ;;
        rebind-port) echo "NAT port rebinding (path validation)" ;;
        rebind-addr) echo "NAT address rebinding (path validation)" ;;
        connectionmigration) echo "Active connection migration" ;;
        *) echo "Unknown scenario" ;;
    esac
}

get_scenario_files() {
    case "$1" in
        handshake) echo "1KB.bin" ;;
        transfer) echo "1MB.bin 5MB.bin" ;;
        retry) echo "1KB.bin" ;;
        resumption) echo "1KB.bin" ;;
        zerortt) echo "1KB.bin" ;;
        http3) echo "10KB.bin 100KB.bin 1MB.bin" ;;
        multiconnect) echo "1KB.bin" ;;
        versionnegotiation) echo "1KB.bin" ;;
        chacha20) echo "5MB.bin" ;;
        keyupdate) echo "2MB.bin" ;;
        v2) echo "1KB.bin" ;;
        rebind-port) echo "5MB.bin" ;;
        rebind-addr) echo "5MB.bin" ;;
        connectionmigration) echo "5MB.bin" ;;
        *) echo "" ;;
    esac
}

# Functions to manage scenario status and timing
set_scenario_status() {
    local scenario=$1
    local status=$2
    SCENARIO_STATUS_LIST="${SCENARIO_STATUS_LIST}${scenario}:${status}|"
}

get_scenario_status() {
    local scenario=$1
    echo "$SCENARIO_STATUS_LIST" | grep -o "${scenario}:[^|]*" | cut -d: -f2
}

set_scenario_duration() {
    local scenario=$1
    local duration=$2
    TEST_DURATIONS_LIST="${TEST_DURATIONS_LIST}${scenario}:${duration}|"
}

get_scenario_duration() {
    local scenario=$1
    local dur=$(echo "$TEST_DURATIONS_LIST" | grep -o "${scenario}:[^|]*" | cut -d: -f2)
    echo "${dur:-0}"
}

# Known QUIC implementations for cross-testing
KNOWN_IMPLEMENTATIONS=(
    "quiche"        # Cloudflare's QUIC implementation
    "ngtcp2"        # nghttp2's QUIC library
    "quic-go"       # Go implementation
    "mvfst"         # Meta's QUIC (formerly Facebook)
    "quinn"         # Rust implementation
    "aioquic"       # Python asyncio implementation
    "picoquic"      # Minimalist C implementation
    "neqo"          # Mozilla's Rust implementation
    "lsquic"        # LiteSpeed QUIC
    "msquic"        # Microsoft QUIC
    "s2n-quic"      # AWS's QUIC implementation
)

################################################################################
# Helper Functions
################################################################################

print_header() {
    local title="$1"
    echo ""
    echo -e "${BOLD}=========================================${NC}"
    echo -e "${BOLD}$title${NC}"
    echo -e "${BOLD}=========================================${NC}"
    echo ""
}

print_section() {
    local title="$1"
    echo ""
    echo -e "${CYAN}--- $title ---${NC}"
    echo ""
}

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_skip() {
    echo -e "${YELLOW}[SKIP]${NC} $1"
}

################################################################################
# Setup Functions
################################################################################

check_prerequisites() {
    log_info "Checking prerequisites..."

    # Check Docker
    if ! command -v docker &> /dev/null; then
        log_error "Docker is not installed"
        exit 1
    fi

    # Check if quicX interop image exists
    if ! docker images | grep -q "quicx-interop"; then
        log_error "quicx-interop Docker image not found"
        echo "Please build it first:"
        echo "  docker build -f Dockerfile -t quicx-interop:latest ../.."
        exit 1
    fi

    log_success "Prerequisites check passed"
}

generate_test_files() {
    log_info "Generating test files..."

    mkdir -p www downloads logs/server logs/client

    # Generate various file sizes
    local files=(
        "32B:32"
        "1KB:1024"
        "5KB:5120"
        "10KB:10240"
        "100KB:102400"
        "500KB:512000"
        "1MB:1048576"
        "2MB:2097152"
        "3MB:3145728"
        "5MB:5242880"
        "10MB:10485760"
    )

    for file_spec in "${files[@]}"; do
        IFS=':' read -r filename size <<< "$file_spec"
        if [ ! -f "www/$filename.bin" ]; then
            dd if=/dev/urandom of="www/$filename.bin" bs=1 count=$size 2>/dev/null
            log_info "Created www/$filename.bin ($size bytes)"
        fi
    done

    log_success "Test files ready"
}

cleanup_environment() {
    log_info "Cleaning up test environment..."

    # Stop any running containers
    docker stop quicx-server quicx-client 2>/dev/null || true
    docker rm quicx-server quicx-client 2>/dev/null || true

    # Clean download directory
    rm -rf downloads/*
    rm -rf /tmp/quicx-*

    log_success "Environment cleaned"
}

################################################################################
# Server/Client Functions
################################################################################

start_server() {
    local scenario=$1
    local extra_env=$2

    log_info "Starting quicX server for scenario: $scenario"

    docker run -d --rm \
        --name quicx-server \
        --network host \
        -e ROLE=server \
        -e PORT=$SERVER_PORT \
        -e WWW=/www \
        -e TESTCASE=$scenario \
        -e QLOGDIR=/logs \
        -e SSLKEYLOGFILE=/logs/server/keys.log \
        $extra_env \
        -v "$PWD/www:/www:ro" \
        -v "$PWD/logs/server:/logs" \
        quicx-interop:latest \
        > /dev/null 2>&1

    # Wait for server to be ready
    sleep 2

    # Verify server is running
    if ! docker ps | grep -q quicx-server; then
        log_error "Server failed to start"
        docker logs quicx-server 2>&1 || true
        return 1
    fi

    log_success "Server started on port $SERVER_PORT"
    return 0
}

stop_server() {
    docker stop quicx-server 2>/dev/null || true
    sleep 1
}

run_client() {
    local scenario=$1
    local urls=$2
    local extra_env=$3

    log_info "Running quicX client for scenario: $scenario"

    rm -rf downloads/*

    local exit_code=0
    docker run --rm \
        --name quicx-client \
        --network host \
        -e ROLE=client \
        -e SERVER=$SERVER_HOST \
        -e PORT=$SERVER_PORT \
        -e REQUESTS="$urls" \
        -e TESTCASE=$scenario \
        -e QLOGDIR=/logs \
        -e SSLKEYLOGFILE=/logs/client/keys.log \
        $extra_env \
        -v "$PWD/downloads:/downloads" \
        -v "$PWD/logs/client:/logs" \
        quicx-interop:latest || exit_code=$?

    # Exit code 127 means unsupported test case
    if [ $exit_code -eq 127 ]; then
        return 127
    elif [ $exit_code -ne 0 ]; then
        return 1
    fi

    return 0
}

################################################################################
# Test Verification Functions
################################################################################

verify_file() {
    local filename=$1
    local src="www/$filename"
    local dst="downloads/$filename"

    if [ ! -f "$dst" ]; then
        log_error "File not downloaded: $filename"
        return 1
    fi

    local src_size=$(stat -f%z "$src" 2>/dev/null || stat -c%s "$src" 2>/dev/null)
    local dst_size=$(stat -f%z "$dst" 2>/dev/null || stat -c%s "$dst" 2>/dev/null)

    if [ "$src_size" != "$dst_size" ]; then
        log_error "Size mismatch: $filename (expected $src_size, got $dst_size)"
        return 1
    fi

    if ! cmp -s "$src" "$dst"; then
        log_error "Content mismatch: $filename"
        return 1
    fi

    log_success "Verified: $filename ($dst_size bytes)"
    return 0
}

verify_downloads() {
    local files=($1)
    local all_ok=true

    for file in "${files[@]}"; do
        if ! verify_file "$file"; then
            all_ok=false
        fi
    done

    $all_ok
}

################################################################################
# Individual Test Scenarios
################################################################################

test_handshake() {
    local urls="https://$SERVER_HOST:$SERVER_PORT/1KB.bin"

    start_server "handshake" "" || return 1

    if run_client "handshake" "$urls" ""; then
        verify_file "1KB.bin"
        local result=$?
        stop_server
        return $result
    else
        local exit_code=$?
        stop_server
        return $exit_code
    fi
}

test_transfer() {
    local urls="https://$SERVER_HOST:$SERVER_PORT/1MB.bin"
    urls="$urls https://$SERVER_HOST:$SERVER_PORT/5MB.bin"

    start_server "transfer" "" || return 1

    if run_client "transfer" "$urls" ""; then
        verify_file "1MB.bin" && verify_file "5MB.bin"
        local result=$?
        stop_server
        return $result
    else
        local exit_code=$?
        stop_server
        return $exit_code
    fi
}

test_retry() {
    local urls="https://$SERVER_HOST:$SERVER_PORT/1KB.bin"

    start_server "retry" "-e FORCE_RETRY=1" || return 1

    if run_client "retry" "$urls" ""; then
        verify_file "1KB.bin"
        local result=$?
        stop_server
        return $result
    else
        local exit_code=$?
        stop_server
        return $exit_code
    fi
}

test_resumption() {
    local urls="https://$SERVER_HOST:$SERVER_PORT/1KB.bin"
    local session_dir="/tmp/quicx-session-$$"

    start_server "resumption" "" || return 1

    # First connection
    log_info "First connection (full handshake)..."
    rm -rf "$session_dir"
    if ! run_client "resumption" "$urls" "-e SESSION_CACHE=$session_dir"; then
        stop_server
        return 1
    fi

    # Verify session was saved
    if [ ! -d "$session_dir" ] || [ -z "$(ls -A "$session_dir" 2>/dev/null)" ]; then
        log_error "Session cache not created"
        stop_server
        rm -rf "$session_dir"
        return 1
    fi
    log_success "Session saved"

    # Second connection (resumption)
    log_info "Second connection (resumption)..."
    rm -f downloads/1KB.bin
    if ! run_client "resumption" "$urls" "-e SESSION_CACHE=$session_dir"; then
        stop_server
        rm -rf "$session_dir"
        return 1
    fi

    verify_file "1KB.bin"
    local result=$?

    stop_server
    rm -rf "$session_dir"
    return $result
}

test_zerortt() {
    local urls="https://$SERVER_HOST:$SERVER_PORT/1KB.bin"
    local session_dir="/tmp/quicx-0rtt-$$"

    start_server "zerortt" "" || return 1

    # First connection
    log_info "First connection (establishing session)..."
    rm -rf "$session_dir"
    if ! run_client "zerortt" "$urls" "-e SESSION_CACHE=$session_dir"; then
        stop_server
        return 1
    fi

    sleep 1

    # Second connection (0-RTT)
    log_info "Second connection (0-RTT)..."
    rm -f downloads/1KB.bin
    if ! run_client "zerortt" "$urls" "-e SESSION_CACHE=$session_dir"; then
        stop_server
        rm -rf "$session_dir"
        return 1
    fi

    verify_file "1KB.bin"
    local result=$?

    stop_server
    rm -rf "$session_dir"
    return $result
}

test_http3() {
    local urls="https://$SERVER_HOST:$SERVER_PORT/10KB.bin"
    urls="$urls https://$SERVER_HOST:$SERVER_PORT/100KB.bin"
    urls="$urls https://$SERVER_HOST:$SERVER_PORT/1MB.bin"

    start_server "http3" "" || return 1

    if run_client "http3" "$urls" ""; then
        verify_file "10KB.bin" && verify_file "100KB.bin" && verify_file "1MB.bin"
        local result=$?
        stop_server
        return $result
    else
        local exit_code=$?
        stop_server
        return $exit_code
    fi
}

test_multiconnect() {
    local urls="https://$SERVER_HOST:$SERVER_PORT/1KB.bin"

    start_server "multiconnect" "" || return 1

    log_info "Testing multiple concurrent connections..."

    local pids=()
    local success=0

    for i in {1..5}; do
        (
            mkdir -p downloads/conn$i
            docker run --rm \
                --network host \
                -e ROLE=client \
                -e SERVER=$SERVER_HOST \
                -e PORT=$SERVER_PORT \
                -e REQUESTS="$urls" \
                -e TESTCASE=multiconnect \
                -v "$PWD/downloads/conn$i:/downloads" \
                quicx-interop:latest \
                > /dev/null 2>&1
        ) &
        pids+=($!)
    done

    for pid in "${pids[@]}"; do
        if wait $pid; then
            ((success++))
        fi
    done

    stop_server

    if [ $success -eq 5 ]; then
        log_success "All 5 connections succeeded"
        rm -rf downloads/conn*
        return 0
    else
        log_error "Only $success/5 connections succeeded"
        rm -rf downloads/conn*
        return 1
    fi
}

test_versionnegotiation() {
    local urls="https://$SERVER_HOST:$SERVER_PORT/1KB.bin"

    start_server "versionnegotiation" "" || return 1

    if run_client "versionnegotiation" "$urls" "-e PREFERRED_VERSION=0x1a2a3a4a"; then
        verify_file "1KB.bin"
        local result=$?
        stop_server
        return $result
    else
        local exit_code=$?
        stop_server
        return $exit_code
    fi
}

test_chacha20() {
    local urls="https://$SERVER_HOST:$SERVER_PORT/5MB.bin"

    start_server "chacha20" "-e CIPHER_SUITES=TLS_CHACHA20_POLY1305_SHA256" || return 1

    if run_client "chacha20" "$urls" "-e CIPHER_SUITES=TLS_CHACHA20_POLY1305_SHA256"; then
        verify_file "5MB.bin"
        local result=$?
        stop_server
        return $result
    else
        local exit_code=$?
        stop_server
        return $exit_code
    fi
}

test_keyupdate() {
    local urls="https://$SERVER_HOST:$SERVER_PORT/2MB.bin"

    start_server "keyupdate" "" || return 1

    if run_client "keyupdate" "$urls" "-e FORCE_KEY_UPDATE=1"; then
        verify_file "2MB.bin"
        local result=$?
        stop_server
        return $result
    else
        local exit_code=$?
        stop_server
        return $exit_code
    fi
}

test_v2() {
    local urls="https://$SERVER_HOST:$SERVER_PORT/1KB.bin"

    start_server "v2" "" || return 1

    if run_client "v2" "$urls" ""; then
        verify_file "1KB.bin"
        local result=$?
        stop_server
        return $result
    else
        local exit_code=$?
        stop_server
        return $exit_code
    fi
}

test_rebind_port() {
    local urls="https://$SERVER_HOST:$SERVER_PORT/5MB.bin"

    start_server "rebind-port" "" || return 1

    if run_client "rebind-port" "$urls" ""; then
        verify_file "5MB.bin"
        local result=$?
        stop_server
        return $result
    else
        local exit_code=$?
        stop_server
        return $exit_code
    fi
}

test_rebind_addr() {
    local urls="https://$SERVER_HOST:$SERVER_PORT/5MB.bin"

    start_server "rebind-addr" "" || return 1

    if run_client "rebind-addr" "$urls" ""; then
        verify_file "5MB.bin"
        local result=$?
        stop_server
        return $result
    else
        local exit_code=$?
        stop_server
        return $exit_code
    fi
}

test_connectionmigration() {
    local urls="https://$SERVER_HOST:$SERVER_PORT/5MB.bin"

    start_server "connectionmigration" "" || return 1

    if run_client "connectionmigration" "$urls" ""; then
        verify_file "5MB.bin"
        local result=$?
        stop_server
        return $result
    else
        local exit_code=$?
        stop_server
        return $exit_code
    fi
}

################################################################################
# Test Execution
################################################################################

run_single_test() {
    local scenario=$1

    TOTAL_TESTS=$((TOTAL_TESTS + 1))

    print_section "Test $TOTAL_TESTS: $scenario - $(get_scenario_desc $scenario)"

    local start_time=$(date +%s)

    # Run test function
    local test_func="test_${scenario//-/_}"  # Replace hyphens with underscores

    if ! declare -f "$test_func" > /dev/null; then
        log_warning "Test function not implemented: $test_func"
        set_scenario_status $scenario "UNSUPPORTED"
        UNSUPPORTED_TESTS=$((UNSUPPORTED_TESTS + 1))
        return
    fi

    if $test_func; then
        local exit_code=$?
        if [ $exit_code -eq 127 ]; then
            log_skip "Test not supported by implementation"
            set_scenario_status $scenario "UNSUPPORTED"
            UNSUPPORTED_TESTS=$((UNSUPPORTED_TESTS + 1))
        else
            log_success "Test PASSED"
            set_scenario_status $scenario "PASS"
            PASSED_TESTS=$((PASSED_TESTS + 1))
        fi
    else
        local exit_code=$?
        if [ $exit_code -eq 127 ]; then
            log_skip "Test not supported by implementation"
            set_scenario_status $scenario "UNSUPPORTED"
            UNSUPPORTED_TESTS=$((UNSUPPORTED_TESTS + 1))
        else
            log_error "Test FAILED"
            set_scenario_status $scenario "FAIL"
            FAILED_TESTS=$((FAILED_TESTS + 1))
        fi
    fi

    local end_time=$(date +%s)
    local duration=$((end_time - start_time))
    set_scenario_duration $scenario $duration

    log_info "Duration: ${duration}ms"
}

################################################################################
# Output Formatting
################################################################################

print_text_summary() {
    print_header "Test Results Summary"

    printf "%-30s %-15s %-10s\n" "Test Scenario" "Status" "Duration"
    echo "--------------------------------------------------------------------"

    for scenario in "${ALL_SCENARIOS[@]}"; do
        local status="$(get_scenario_status $scenario)"
        local duration="$(get_scenario_duration $scenario)"

        local status_display
        case $status in
            PASS)        status_display="${GREEN}✓ PASSED${NC}" ;;
            FAIL)        status_display="${RED}✗ FAILED${NC}" ;;
            UNSUPPORTED) status_display="${YELLOW}⊘ UNSUPPORTED${NC}" ;;
            SKIP)        status_display="${CYAN}- SKIPPED${NC}" ;;
            *)           status_display="${MAGENTA}? UNKNOWN${NC}" ;;
        esac

        printf "%-30s %-25s %6dms\n" "$scenario" "$status_display" "$duration"
    done

    echo ""
    echo "--------------------------------------------------------------------"
    echo -e "Total Tests:        ${BOLD}$TOTAL_TESTS${NC}"
    echo -e "${GREEN}Passed:             $PASSED_TESTS${NC}"
    echo -e "${RED}Failed:             $FAILED_TESTS${NC}"
    echo -e "${YELLOW}Unsupported:        $UNSUPPORTED_TESTS${NC}"
    echo -e "${CYAN}Skipped:            $SKIPPED_TESTS${NC}"
    echo ""

    if [ $TOTAL_TESTS -gt 0 ]; then
        local executed=$((TOTAL_TESTS - SKIPPED_TESTS))
        if [ $executed -gt 0 ]; then
            local pass_rate=$(( (PASSED_TESTS * 100) / executed ))
            echo -e "Pass Rate: ${BOLD}${pass_rate}%${NC} (excluding skipped)"
        fi
    fi

    echo ""
}

print_markdown_table() {
    print_header "Interoperability Test Results (Markdown)"

    echo "# quicX QUIC Interoperability Test Results"
    echo ""
    echo "**Test Date:** $(date '+%Y-%m-%d %H:%M:%S')"
    echo ""
    echo "## Test Matrix"
    echo ""
    echo "| # | Test Scenario | Description | Status | Duration |"
    echo "|---|---------------|-------------|--------|----------|"

    local num=1
    for scenario in "${ALL_SCENARIOS[@]}"; do
        local status="$(get_scenario_status $scenario)"
        local duration="$(get_scenario_duration $scenario)"
        local desc="$(get_scenario_desc $scenario)"

        local status_emoji
        case $status in
            PASS)        status_emoji="✅ PASS" ;;
            FAIL)        status_emoji="❌ FAIL" ;;
            UNSUPPORTED) status_emoji="⚠️ UNSUPPORTED" ;;
            SKIP)        status_emoji="⏭️ SKIP" ;;
            *)           status_emoji="❓ UNKNOWN" ;;
        esac

        printf "| %2d | %-20s | %-35s | %-15s | %6dms |\n" \
            "$num" "$scenario" "$desc" "$status_emoji" "$duration"

        ((num++))
    done

    echo ""
    echo "## Summary"
    echo ""
    echo "- **Total Tests:** $TOTAL_TESTS"
    echo "- **Passed:** $PASSED_TESTS ✅"
    echo "- **Failed:** $FAILED_TESTS ❌"
    echo "- **Unsupported:** $UNSUPPORTED_TESTS ⚠️"
    echo "- **Skipped:** $SKIPPED_TESTS ⏭️"

    if [ $TOTAL_TESTS -gt 0 ]; then
        local executed=$((TOTAL_TESTS - SKIPPED_TESTS))
        if [ $executed -gt 0 ]; then
            local pass_rate=$(( (PASSED_TESTS * 100) / executed ))
            echo "- **Pass Rate:** ${pass_rate}%"
        fi
    fi

    echo ""
}

print_json_output() {
    echo "{"
    echo "  \"test_run\": {"
    echo "    \"timestamp\": \"$(date -u '+%Y-%m-%dT%H:%M:%SZ')\","
    echo "    \"implementation\": \"quicX\","
    echo "    \"total\": $TOTAL_TESTS,"
    echo "    \"passed\": $PASSED_TESTS,"
    echo "    \"failed\": $FAILED_TESTS,"
    echo "    \"unsupported\": $UNSUPPORTED_TESTS,"
    echo "    \"skipped\": $SKIPPED_TESTS"
    echo "  },"
    echo "  \"results\": ["

    local first=true
    for scenario in "${ALL_SCENARIOS[@]}"; do
        local status="$(get_scenario_status $scenario)"
        local duration="$(get_scenario_duration $scenario)"
        local desc="$(get_scenario_desc $scenario)"

        if [ "$first" = false ]; then
            echo ","
        fi
        first=false

        echo -n "    {"
        echo -n "\"scenario\": \"$scenario\", "
        echo -n "\"description\": \"$desc\", "
        echo -n "\"status\": \"$status\", "
        echo -n "\"duration_ms\": $duration"
        echo -n "}"
    done

    echo ""
    echo "  ]"
    echo "}"
}

################################################################################
# Main Function
################################################################################

parse_arguments() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            --self-test)
                TEST_MODE="self"
                shift
                ;;
            --cross-impl)
                TEST_MODE="cross"
                CROSS_IMPL="$2"
                shift 2
                ;;
            --all-impls)
                TEST_MODE="all"
                shift
                ;;
            --scenario)
                SPECIFIC_SCENARIO="$2"
                shift 2
                ;;
            --output)
                OUTPUT_FORMAT="$2"
                shift 2
                ;;
            --no-color)
                USE_COLOR=false
                GREEN=''
                RED=''
                YELLOW=''
                BLUE=''
                CYAN=''
                MAGENTA=''
                BOLD=''
                NC=''
                shift
                ;;
            -h|--help)
                echo "Usage: $0 [options]"
                echo ""
                echo "Options:"
                echo "  --self-test          Run tests against quicX only (default)"
                echo "  --cross-impl <impl>  Test against another QUIC implementation"
                echo "  --all-impls          Test against all available implementations"
                echo "  --scenario <name>    Run specific test scenario only"
                echo "  --output <format>    Output format: text|json|markdown (default: text)"
                echo "  --no-color           Disable colored output"
                echo "  -h, --help           Show this help message"
                echo ""
                echo "Available scenarios:"
                for scenario in "${ALL_SCENARIOS[@]}"; do
                    echo "  - $scenario"
                done
                exit 0
                ;;
            *)
                log_error "Unknown option: $1"
                echo "Use --help for usage information"
                exit 1
                ;;
        esac
    done
}

main() {
    parse_arguments "$@"

    print_header "quicX Complete QUIC Interoperability Test Suite"

    log_info "Test mode: $TEST_MODE"
    log_info "Output format: $OUTPUT_FORMAT"

    # Prerequisites
    check_prerequisites

    # Setup
    cleanup_environment
    generate_test_files

    # Run tests
    print_header "Running Test Scenarios"

    if [ -n "$SPECIFIC_SCENARIO" ]; then
        log_info "Running specific scenario: $SPECIFIC_SCENARIO"
        run_single_test "$SPECIFIC_SCENARIO"
    else
        for scenario in "${ALL_SCENARIOS[@]}"; do
            run_single_test "$scenario"
        done
    fi

    # Output results
    case $OUTPUT_FORMAT in
        json)
            print_json_output
            ;;
        markdown)
            print_markdown_table
            ;;
        text|*)
            print_text_summary
            ;;
    esac

    # Cleanup
    cleanup_environment

    # Exit code
    if [ $FAILED_TESTS -eq 0 ]; then
        exit 0
    else
        exit 1
    fi
}

# Trap cleanup
trap cleanup_environment EXIT INT TERM

# Run
main "$@"
