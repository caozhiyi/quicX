# Dockerfile for quic-interop-runner
# This container will be used by https://github.com/marten-seemann/quic-interop-runner
# to test QUIC interoperability with other implementations

FROM ubuntu:22.04

# Set noninteractive mode to avoid prompts during build
ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libssl-dev \
    wget \
    net-tools \
    tcpdump \
    tshark \
    iptables \
    ethtool \
    iproute2 \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /quicx

# Copy source code
COPY . .

# Build quicX
# Clean any existing build artifacts and build fresh
# Only build interop test binaries (disable examples and other tests)
RUN rm -rf build && \
    mkdir -p build && \
    cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release \
             -DBUILD_EXAMPLES=OFF \
             -DENABLE_TESTING=OFF \
             -DENABLE_BENCHMARKS=OFF \
             -DENABLE_FUZZING=OFF \
             -DENABLE_CC_SIMULATOR=OFF \
             -DENABLE_INTERGRATION=OFF \
             -DENABLE_INTEROP=ON && \
    cmake --build . --target interop_server --target interop_client -j4

# Create directories for test files
RUN mkdir -p /www /downloads /logs

# Generate test files (required by interop runner)
# 1MB, 10MB test files
RUN dd if=/dev/urandom of=/www/1MB.bin bs=1M count=1 && \
    dd if=/dev/urandom of=/www/10MB.bin bs=1M count=10

# Copy run script
COPY test/interop/run_endpoint.sh /run_endpoint.sh
RUN chmod +x /run_endpoint.sh

# Expose QUIC port (UDP 443)
EXPOSE 443/udp

# Set entrypoint
ENTRYPOINT ["/run_endpoint.sh"]
