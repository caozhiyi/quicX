#!/bin/bash

# Simple build script for QUIC fuzz tests

set -e

echo "=== Building QUIC Fuzz Tests ==="

# Create build directory
mkdir -p build
cd build

# Configure with clang
# Build from repo root enabling fuzz targets. Respect env flags:
#   ENABLE_LIBFUZZER=ON|OFF, FUZZ_USE_CLANG=ON|OFF
cmake -DENABLE_FUZZING=ON \
      -DENABLE_LIBFUZZER=${ENABLE_LIBFUZZER:-ON} \
      -DFUZZ_USE_CLANG=${FUZZ_USE_CLANG:-ON} \
      ../..

# Build
make -j$(nproc)

echo "=== Build completed ==="
echo ""
echo "Available fuzz tests:"
echo "  ./quic_header_parser/long_header_fuzz"
echo ""
echo "Run with: ./quic_header_parser/long_header_fuzz -artifact_prefix=./crashes/ corpus/"
