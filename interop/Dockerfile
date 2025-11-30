# Dockerfile for quic-interop-runner
# This container will be used by https://github.com/marten-seemann/quic-interop-runner
# to test QUIC interoperability with other implementations

FROM ubuntu:22.04

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
RUN mkdir -p build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    cmake --build . -j$(nproc)

# Create directories for test files
RUN mkdir -p /www /downloads /logs

# Generate test files (required by interop runner)
# 1MB, 10MB test files
RUN dd if=/dev/urandom of=/www/1MB.bin bs=1M count=1 && \
    dd if=/dev/urandom of=/www/10MB.bin bs=1M count=10

# Copy run script
COPY interop/run_endpoint.sh /run_endpoint.sh
RUN chmod +x /run_endpoint.sh

# Expose QUIC port (UDP 443)
EXPOSE 443/udp

# Set entrypoint
ENTRYPOINT ["/run_endpoint.sh"]
