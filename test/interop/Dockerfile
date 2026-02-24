# Dockerfile for quic-interop-runner
# Fully compatible with official quic-network-simulator
# Reference: https://github.com/quic-interop/quic-network-simulator#building-your-own-quic-docker-image

# ============================================================
# Stage 1: Build quicX
# ============================================================
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential cmake git libssl-dev wget curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Copy only necessary files for build
COPY CMakeLists.txt ./
COPY src ./src
COPY third ./third
COPY test/interop ./test/interop

# Build quicX interop binaries
RUN rm -rf build && mkdir -p build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release \
             -DBUILD_EXAMPLES=OFF \
             -DENABLE_TESTING=OFF \
             -DENABLE_BENCHMARKS=OFF \
             -DENABLE_FUZZING=OFF \
             -DENABLE_CC_SIMULATOR=OFF \
             -DENABLE_INTERGRATION=OFF \
             -DENABLE_INTEROP=ON && \
    cmake --build . --target interop_server interop_client -j$(nproc)

# ============================================================
# Stage 2: Runtime image
# Must be based on the official quic-network-simulator endpoint
# This provides /setup.sh and /wait-for-it.sh
# ============================================================
FROM martenseemann/quic-network-simulator-endpoint:latest

ENV DEBIAN_FRONTEND=noninteractive

# Install runtime dependencies (OpenSSL)
RUN apt-get update && apt-get install -y \
    libssl3 ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Copy built binaries from builder stage
COPY --from=builder /src/build/bin/interop_server /usr/local/bin/
COPY --from=builder /src/build/bin/interop_client /usr/local/bin/

# Create required directories (official convention)
# /www       - server web root (mounted read-only by runner)
# /downloads - client download directory (mounted read-write by runner)
# /certs     - TLS certificates (mounted read-only by runner)
# /logs      - log output directory
RUN mkdir -p /www /downloads /logs/qlog /certs

# Save Git commit info for debugging
ARG GIT_COMMIT=unknown
RUN echo "${GIT_COMMIT}" > /commit.txt

# Copy endpoint script
COPY test/interop/run_endpoint.sh /run_endpoint.sh
RUN chmod +x /run_endpoint.sh

# Expose QUIC port (UDP 443)
EXPOSE 443/udp

ENTRYPOINT ["/run_endpoint.sh"]
