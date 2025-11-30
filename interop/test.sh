#!/bin/bash
# Local testing script for quicX interop

set -e

cd "$(dirname "$0")/.."

echo "=================================="
echo "quicX QUIC Interoperability Test"
echo "=================================="
echo ""

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test functions
test_passed() {
    echo -e "${GREEN}✓${NC} $1"
}

test_failed() {
    echo -e "${RED}✗${NC} $1"
}

test_info() {
    echo -e "${YELLOW}ℹ${NC} $1"
}

# Build Docker image
echo "Step 1: Building Docker image..."
if docker build -f interop/Dockerfile -t quicx-interop:latest . > /dev/null 2>&1; then
    test_passed "Docker image built successfully"
else
    test_failed "Docker image build failed"
    exit 1
fi

# Create test directories
mkdir -p interop/logs/server interop/logs/client interop/downloads interop/www

# Generate test files
test_info "Generating test files..."
dd if=/dev/urandom of=interop/www/1MB.bin bs=1M count=1 2>/dev/null
dd if=/dev/urandom of=interop/www/10MB.bin bs=1M count=10 2>/dev/null
test_passed "Test files generated"

# Test 1: Handshake
echo ""
echo "Test 1: Basic Handshake"
echo "------------------------"

# Start server
test_info "Starting server..."
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
if docker ps | grep -q quicx-test-server; then
    test_passed "Server started"
else
    test_failed "Server failed to start"
    docker logs quicx-test-server
    exit 1
fi

# Run client
test_info "Running client..."
if docker run --rm \
    --name quicx-test-client \
    -e ROLE=client \
    -e TESTCASE=handshake \
    -e SERVER=host.docker.internal \
    -e PORT=4433 \
    -e REQUESTS="https://host.docker.internal:4433/1MB.bin" \
    -v "$(pwd)/interop/downloads:/downloads" \
    -v "$(pwd)/interop/logs/client:/logs" \
    quicx-interop:latest > /dev/null 2>&1; then
    test_passed "Client connected successfully"
else
    test_failed "Client connection failed"
    docker logs quicx-test-client || true
fi

# Verify downloaded file
if [ -f "interop/downloads/1MB.bin" ]; then
    SIZE=$(stat -c%s "interop/downloads/1MB.bin" 2>/dev/null || stat -f%z "interop/downloads/1MB.bin")
    if [ "$SIZE" -eq 1048576 ]; then
        test_passed "File downloaded correctly (1 MB)"
    else
        test_failed "Downloaded file size mismatch"
    fi
else
    test_failed "File not downloaded"
fi

# Stop server
docker stop quicx-test-server > /dev/null 2>&1 || true

# Test 2: HTTP/3
echo ""
echo "Test 2: HTTP/3 Transfer"
echo "------------------------"

# Start server with HTTP/3
test_info "Starting HTTP/3 server..."
docker run -d --rm \
    --name quicx-test-server \
    -e ROLE=server \
    -e TESTCASE=http3 \
    -e PORT=443 \
    -v "$(pwd)/interop/www:/www" \
    -p 4433:443/udp \
    quicx-interop:latest > /dev/null 2>&1

sleep 2

# Run HTTP/3 client
test_info "Running HTTP/3 client..."
if docker run --rm \
    --name quicx-test-client \
    -e ROLE=client \
    -e TESTCASE=http3 \
    -e SERVER=host.docker.internal \
    -e PORT=4433 \
    -e REQUESTS="https://host.docker.internal:4433/10MB.bin" \
    -v "$(pwd)/interop/downloads:/downloads" \
    quicx-interop:latest > /dev/null 2>&1; then
    test_passed "HTTP/3 transfer completed"
else
    test_failed "HTTP/3 transfer failed"
fi

# Verify large file
if [ -f "interop/downloads/10MB.bin" ]; then
    SIZE=$(stat -c%s "interop/downloads/10MB.bin" 2>/dev/null || stat -f%z "interop/downloads/10MB.bin")
    if [ "$SIZE" -eq 10485760 ]; then
        test_passed "Large file downloaded correctly (10 MB)"
    else
        test_failed "Large file size mismatch"
    fi
else
    test_failed "Large file not downloaded"
fi

# Stop server
docker stop quicx-test-server > /dev/null 2>&1 || true

# Summary
echo ""
echo "=================================="
echo "Test Summary"
echo "=================================="
echo ""
test_info "Logs available in: interop/logs/"
test_info "Downloads available in: interop/downloads/"
echo ""
test_passed "All tests completed!"
echo ""
echo "To run with quic-interop-runner:"
echo "  1. git clone https://github.com/marten-seemann/quic-interop-runner.git"
echo "  2. cd quic-interop-runner"
echo "  3. cp ../quicX/interop/manifest.json implementations/quicx.json"
echo "  4. python3 run.py -s quicx -c quicx"
echo ""
