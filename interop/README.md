# QUIC Interoperability Testing for quicX

This directory contains configuration and tools for running quicX with the [quic-interop-runner](https://github.com/marten-seemann/quic-interop-runner).

## Overview

The quic-interop-runner is the official QUIC interoperability testing framework that tests compatibility between different QUIC implementations. Results are published at https://interop.seemann.io/

## Quick Start

### Build Docker Image

```bash
cd /mnt/d/code/quicX
docker build -f interop/Dockerfile -t quicx-interop:latest .
```

### Test Locally

Run server:
```bash
docker run -it --rm \
  -e ROLE=server \
  -e TESTCASE=handshake \
  -p 443:443/udp \
  quicx-interop:latest
```

Run client (in another terminal):
```bash
docker run -it --rm \
  -e ROLE=client \
  -e TESTCASE=handshake \
  -e SERVER=host.docker.internal \
  -e PORT=443 \
  -e REQUESTS="https://host.docker.internal:443/1MB.bin" \
  quicx-interop:latest
```

### Run with quic-interop-runner

1. Clone the interop runner:
```bash
git clone https://github.com/marten-seemann/quic-interop-runner.git
cd quic-interop-runner
```

2. Add quicX to implementations:
```bash
cp /mnt/d/code/quicX/interop/manifest.json implementations/quicx.json
```

3. Run tests:
```bash
# Test quicX server with other clients
python3 run.py -s quicx -c quic-go,ngtcp2,mvfst

# Test quicX client with other servers
python3 run.py -s quic-go,ngtcp2,mvfst -c quicx

# Run all test cases
python3 run.py -s quicx -c quicx -t handshake,transfer,retry,resumption
```

## Supported Test Cases

| Test Case | Description | Status |
|-----------|-------------|--------|
| `handshake` | Basic QUIC handshake | ✅ Supported |
| `transfer` | File transfer (1MB, 10MB) | ✅ Supported |
| `retry` | Retry packet handling | 🚧 Planned |
| `resumption` | Session resumption | 🚧 Planned |
| `zerortt` | 0-RTT data | 🚧 Planned |
| `http3` | HTTP/3 functionality | ✅ Supported |
| `multiconnect` | Multiple connections | ✅ Supported |
| `versionnegotiation` | Version negotiation | 🚧 Planned |
| `chacha20` | ChaCha20-Poly1305 cipher | 🚧 Planned |

## Environment Variables

### Server Mode

- `ROLE=server` - Run as server
- `TESTCASE` - Test scenario name
- `PORT` - Port to listen on (default: 443)
- `WWW` - Directory containing files to serve
- `SSLKEYLOGFILE` - TLS key log file path (for Wireshark)
- `QLOGDIR` - Directory for qlog files

### Client Mode

- `ROLE=client` - Run as client
- `TESTCASE` - Test scenario name
- `SERVER` - Server hostname
- `PORT` - Server port
- `REQUESTS` - Space-separated URLs to download
- `DOWNLOADS` - Download directory
- `SSLKEYLOGFILE` - TLS key log file path
- `QLOGDIR` - Directory for qlog files

## Directory Structure

```
interop/
├── Dockerfile              # Docker image for interop testing
├── run_endpoint.sh         # Entrypoint script (server/client)
├── manifest.json           # Implementation metadata
├── README.md              # This file
└── src/                   # Interop test program source
    ├── interop_server.cpp # Test server implementation
    └── interop_client.cpp # Test client implementation
```

## Implementation Notes

### Required Functionality

1. **Server Requirements**:
   - Listen on UDP port 443
   - Serve files from `/www` directory
   - Support HTTPS/HTTP3
   - Handle multiple concurrent connections
   - Generate/use TLS certificates

2. **Client Requirements**:
   - Connect to specified server
   - Download requested URLs
   - Save files to `/downloads` directory
   - Support HTTP/3
   - Handle connection migration

### Test File Preparation

The Docker image automatically generates test files:
- `/www/1MB.bin` - 1 MB random data
- `/www/10MB.bin` - 10 MB random data

### Logging and Debugging

- Set `QLOGDIR=/logs` to enable qlog output
- Set `SSLKEYLOGFILE=/logs/keys.log` for TLS key logging
- Use `tcpdump` in container for packet capture:
  ```bash
  docker exec <container> tcpdump -i any -w /logs/capture.pcap udp port 443
  ```

## Troubleshooting

### Build Issues

If Docker build fails:
```bash
# Clean build
docker build --no-cache -f interop/Dockerfile -t quicx-interop:latest .

# Check build logs
docker build -f interop/Dockerfile -t quicx-interop:latest . 2>&1 | tee build.log
```

### Network Issues

Ensure UDP port 443 is accessible:
```bash
# Check firewall
sudo ufw allow 443/udp

# Test UDP connectivity
nc -u -l 443  # On server
nc -u <server-ip> 443  # On client
```

### Container Debugging

Enter container for debugging:
```bash
docker run -it --rm --entrypoint /bin/bash quicx-interop:latest
```

## Contributing

When adding support for new test cases:

1. Update `run_endpoint.sh` with test-specific logic
2. Add test case to `manifest.json`
3. Implement required functionality in quicX
4. Test locally before submitting
5. Update this README with status

## References

- [QUIC Interop Runner](https://github.com/marten-seemann/quic-interop-runner)
- [Interop Results](https://interop.seemann.io/)
- [QUIC RFC 9000](https://www.rfc-editor.org/rfc/rfc9000.html)
- [HTTP/3 RFC 9114](https://www.rfc-editor.org/rfc/rfc9114.html)
