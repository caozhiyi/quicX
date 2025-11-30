#!/bin/bash
# Comprehensive interop testing script for quicX
# Tests all supported QUIC interop scenarios

set -e

cd "$(dirname "$0")/.."

echo "=========================================="
echo "quicX QUIC Comprehensive Interop Tests"
echo "=========================================="
echo ""

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test counters
TESTS_TOTAL=0
TESTS_PASSED=0
TESTS_FAILED=0
TESTS_SKIPPED=0

# Test functions
test_passed() {
    echo -e "${GREEN}✓ PASS${NC} $1"
    ((TESTS_PASSED++))
}

test_failed() {
    echo -e "${RED}✗ FAIL${NC} $1"
    ((TESTS_FAILED++))
}

test_skipped() {
    echo -e "${YELLOW}⊘ SKIP${NC} $1"
    ((TESTS_SKIPPED++))
}

test_info() {
    echo -e "${BLUE}ℹ INFO${NC} $1"
}

run_test() {
    local test_name=$1
    local test_func=$2

    echo ""
    echo "=========================================="
    echo "Test: $test_name"
    echo "=========================================="
    ((TESTS_TOTAL++))

    if $test_func; then
        test_passed "$test_name"
    else
        test_failed "$test_name"
    fi
}

# Cleanup function
cleanup() {
    docker stop quicx-test-server 2>/dev/null || true
    docker rm quicx-test-server 2>/dev/null || true
}

# Build Docker image
build_image() {
    echo "Building Docker image..."
    if docker build -f interop/Dockerfile -t quicx-interop:latest . > /dev/null 2>&1; then
        test_info "Docker image built successfully"
    else
        echo -e "${RED}ERROR: Docker image build failed${NC}"
        exit 1
    fi
}

# Setup test environment
setup_test_env() {
    test_info "Setting up test environment..."

    # Create directories
    mkdir -p interop/logs/server interop/logs/client
    mkdir -p interop/downloads interop/www

    # Generate test files
    if [ ! -f interop/www/1MB.bin ]; then
        dd if=/dev/urandom of=interop/www/1MB.bin bs=1M count=1 2>/dev/null
    fi
    if [ ! -f interop/www/10MB.bin ]; then
        dd if=/dev/urandom of=interop/www/10MB.bin bs=1M count=10 2>/dev/null
    fi
    if [ ! -f interop/www/index.html ]; then
        echo "<html><body>quicX Interop Test</body></html>" > interop/www/index.html
    fi

    # Generate checksums for verification
    md5sum interop/www/1MB.bin > interop/www/1MB.bin.md5
    md5sum interop/www/10MB.bin > interop/www/10MB.bin.md5

    test_info "Test files ready"
}

# Test 1: Basic Handshake
test_handshake() {
    test_info "Starting handshake test..."

    # Start server
    docker run -d --rm \
        --name quicx-test-server \
        -e ROLE=server \
        -e TESTCASE=handshake \
        -e PORT=443 \
        -v "$(pwd)/interop/www:/www" \
        -v "$(pwd)/interop/logs/server:/logs" \
        -p 4433:443/udp \
        quicx-interop:latest > /dev/null 2>&1

    sleep 2

    # Check server is running
    if ! docker ps | grep -q quicx-test-server; then
        test_info "Server failed to start"
        docker logs quicx-test-server 2>&1 | tail -20
        cleanup
        return 1
    fi

    # Run client
    if docker run --rm \
        --name quicx-test-client \
        -e ROLE=client \
        -e TESTCASE=handshake \
        -e SERVER=host.docker.internal \
        -e PORT=4433 \
        -e REQUESTS="https://host.docker.internal:4433/index.html" \
        -v "$(pwd)/interop/downloads:/downloads" \
        -v "$(pwd)/interop/logs/client:/logs" \
        quicx-interop:latest > /dev/null 2>&1; then
        cleanup
        return 0
    else
        cleanup
        return 1
    fi
}

# Test 2: File Transfer (1MB)
test_transfer_1mb() {
    test_info "Starting 1MB transfer test..."

    rm -f interop/downloads/1MB.bin

    # Start server
    docker run -d --rm \
        --name quicx-test-server \
        -e ROLE=server \
        -e TESTCASE=transfer \
        -e PORT=443 \
        -v "$(pwd)/interop/www:/www" \
        -v "$(pwd)/interop/logs/server:/logs" \
        -p 4433:443/udp \
        quicx-interop:latest > /dev/null 2>&1

    sleep 2

    # Run client
    if ! docker run --rm \
        --name quicx-test-client \
        -e ROLE=client \
        -e TESTCASE=transfer \
        -e SERVER=host.docker.internal \
        -e PORT=4433 \
        -e REQUESTS="https://host.docker.internal:4433/1MB.bin" \
        -v "$(pwd)/interop/downloads:/downloads" \
        -v "$(pwd)/interop/logs/client:/logs" \
        quicx-interop:latest > /dev/null 2>&1; then
        cleanup
        return 1
    fi

    cleanup

    # Verify file
    if [ ! -f "interop/downloads/1MB.bin" ]; then
        test_info "File not downloaded"
        return 1
    fi

    # Check size
    SIZE=$(stat -c%s "interop/downloads/1MB.bin" 2>/dev/null || stat -f%z "interop/downloads/1MB.bin")
    if [ "$SIZE" -ne 1048576 ]; then
        test_info "File size mismatch: expected 1048576, got $SIZE"
        return 1
    fi

    # Verify checksum
    if ! (cd interop/downloads && md5sum -c ../www/1MB.bin.md5 > /dev/null 2>&1); then
        test_info "Checksum verification failed"
        return 1
    fi

    return 0
}

# Test 3: File Transfer (10MB)
test_transfer_10mb() {
    test_info "Starting 10MB transfer test..."

    rm -f interop/downloads/10MB.bin

    # Start server
    docker run -d --rm \
        --name quicx-test-server \
        -e ROLE=server \
        -e TESTCASE=transfer \
        -e PORT=443 \
        -v "$(pwd)/interop/www:/www" \
        -v "$(pwd)/interop/logs/server:/logs" \
        -p 4433:443/udp \
        quicx-interop:latest > /dev/null 2>&1

    sleep 2

    # Run client
    if ! docker run --rm \
        --name quicx-test-client \
        -e ROLE=client \
        -e TESTCASE=transfer \
        -e SERVER=host.docker.internal \
        -e PORT=4433 \
        -e REQUESTS="https://host.docker.internal:4433/10MB.bin" \
        -v "$(pwd)/interop/downloads:/downloads" \
        -v "$(pwd)/interop/logs/client:/logs" \
        quicx-interop:latest > /dev/null 2>&1; then
        cleanup
        return 1
    fi

    cleanup

    # Verify file
    if [ ! -f "interop/downloads/10MB.bin" ]; then
        test_info "File not downloaded"
        return 1
    fi

    # Check size
    SIZE=$(stat -c%s "interop/downloads/10MB.bin" 2>/dev/null || stat -f%z "interop/downloads/10MB.bin")
    if [ "$SIZE" -ne 10485760 ]; then
        test_info "File size mismatch: expected 10485760, got $SIZE"
        return 1
    fi

    # Verify checksum
    if ! (cd interop/downloads && md5sum -c ../www/10MB.bin.md5 > /dev/null 2>&1); then
        test_info "Checksum verification failed"
        return 1
    fi

    return 0
}

# Test 4: HTTP/3 Multiple Requests
test_http3() {
    test_info "Starting HTTP/3 test (multiple requests)..."

    rm -f interop/downloads/*.bin interop/downloads/*.html

    # Start server
    docker run -d --rm \
        --name quicx-test-server \
        -e ROLE=server \
        -e TESTCASE=http3 \
        -e PORT=443 \
        -v "$(pwd)/interop/www:/www" \
        -v "$(pwd)/interop/logs/server:/logs" \
        -p 4433:443/udp \
        quicx-interop:latest > /dev/null 2>&1

    sleep 2

    # Run client with multiple requests
    if ! docker run --rm \
        --name quicx-test-client \
        -e ROLE=client \
        -e TESTCASE=http3 \
        -e SERVER=host.docker.internal \
        -e PORT=4433 \
        -e REQUESTS="https://host.docker.internal:4433/index.html https://host.docker.internal:4433/1MB.bin https://host.docker.internal:4433/10MB.bin" \
        -v "$(pwd)/interop/downloads:/downloads" \
        -v "$(pwd)/interop/logs/client:/logs" \
        quicx-interop:latest > /dev/null 2>&1; then
        cleanup
        return 1
    fi

    cleanup

    # Verify all files downloaded
    if [ ! -f "interop/downloads/index.html" ] || \
       [ ! -f "interop/downloads/1MB.bin" ] || \
       [ ! -f "interop/downloads/10MB.bin" ]; then
        test_info "Not all files downloaded"
        return 1
    fi

    return 0
}

# Test 5: ECN Support
test_ecn() {
    test_info "Starting ECN test..."

    rm -f interop/downloads/1MB.bin

    # Start server with ECN
    docker run -d --rm \
        --name quicx-test-server \
        -e ROLE=server \
        -e TESTCASE=ecn \
        -e PORT=443 \
        -e ENABLE_ECN=1 \
        -v "$(pwd)/interop/www:/www" \
        -v "$(pwd)/interop/logs/server:/logs" \
        -p 4433:443/udp \
        quicx-interop:latest > /dev/null 2>&1

    sleep 2

    # Run client with ECN
    if ! docker run --rm \
        --name quicx-test-client \
        -e ROLE=client \
        -e TESTCASE=ecn \
        -e SERVER=host.docker.internal \
        -e PORT=4433 \
        -e ENABLE_ECN=1 \
        -e REQUESTS="https://host.docker.internal:4433/1MB.bin" \
        -v "$(pwd)/interop/downloads:/downloads" \
        -v "$(pwd)/interop/logs/client:/logs" \
        quicx-interop:latest > /dev/null 2>&1; then
        cleanup
        return 1
    fi

    cleanup

    # Basic verification - file downloaded
    if [ ! -f "interop/downloads/1MB.bin" ]; then
        return 1
    fi

    return 0
}

# Test 6: SSLKEYLOG Support
test_sslkeylog() {
    test_info "Starting SSLKEYLOG test..."

    rm -f interop/logs/server/keys.log interop/logs/client/keys.log
    rm -f interop/downloads/1MB.bin

    # Start server with SSLKEYLOG
    docker run -d --rm \
        --name quicx-test-server \
        -e ROLE=server \
        -e TESTCASE=handshake \
        -e PORT=443 \
        -e SSLKEYLOGFILE=/logs/keys.log \
        -v "$(pwd)/interop/www:/www" \
        -v "$(pwd)/interop/logs/server:/logs" \
        -p 4433:443/udp \
        quicx-interop:latest > /dev/null 2>&1

    sleep 2

    # Run client with SSLKEYLOG
    if ! docker run --rm \
        --name quicx-test-client \
        -e ROLE=client \
        -e TESTCASE=handshake \
        -e SERVER=host.docker.internal \
        -e PORT=4433 \
        -e SSLKEYLOGFILE=/logs/keys.log \
        -e REQUESTS="https://host.docker.internal:4433/1MB.bin" \
        -v "$(pwd)/interop/downloads:/downloads" \
        -v "$(pwd)/interop/logs/client:/logs" \
        quicx-interop:latest > /dev/null 2>&1; then
        cleanup
        return 1
    fi

    cleanup

    # Check if keylog files were created
    if [ -f "interop/logs/server/keys.log" ] && [ -f "interop/logs/client/keys.log" ]; then
        test_info "SSLKEYLOG files created"
        return 0
    else
        test_info "SSLKEYLOG files not created (feature may not be fully implemented)"
        return 1
    fi
}

# Main test execution
main() {
    # Build image first
    build_image

    # Setup environment
    setup_test_env

    # Run tests
    run_test "Handshake" test_handshake
    run_test "Transfer-1MB" test_transfer_1mb
    run_test "Transfer-10MB" test_transfer_10mb
    run_test "HTTP/3" test_http3
    run_test "ECN" test_ecn
    run_test "SSLKEYLOG" test_sslkeylog

    # Summary
    echo ""
    echo "=========================================="
    echo "Test Summary"
    echo "=========================================="
    echo -e "Total:   ${TESTS_TOTAL}"
    echo -e "${GREEN}Passed:  ${TESTS_PASSED}${NC}"
    echo -e "${RED}Failed:  ${TESTS_FAILED}${NC}"
    echo -e "${YELLOW}Skipped: ${TESTS_SKIPPED}${NC}"
    echo ""

    if [ $TESTS_FAILED -eq 0 ]; then
        echo -e "${GREEN}All tests passed!${NC}"
        echo ""
        echo "Next steps:"
        echo "  1. Test against other implementations:"
        echo "     cd quic-interop-runner"
        echo "     python3 run.py -s quicx -c quic-go,ngtcp2,mvfst"
        echo ""
        echo "  2. Submit results to https://interop.seemann.io/"
        exit 0
    else
        echo -e "${RED}Some tests failed. Check logs in interop/logs/${NC}"
        exit 1
    fi
}

# Cleanup on exit
trap cleanup EXIT

# Run main
main
