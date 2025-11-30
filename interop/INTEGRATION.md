# Integration with quic-interop-runner

## Setup Instructions

### 1. Build Docker Image

From the quicX root directory:

```bash
docker build -f interop/Dockerfile -t quicx-interop:latest .
```

### 2. Test Locally

```bash
# Run local tests
./interop/test.sh

# Or use docker-compose
docker-compose -f interop/docker-compose.yml up server
docker-compose -f interop/docker-compose.yml run client
```

### 3. Submit to quic-interop-runner

1. Fork the quic-interop-runner repository:
```bash
git clone https://github.com/marten-seemann/quic-interop-runner.git
cd quic-interop-runner
```

2. Add quicX implementation:
```bash
cp /path/to/quicX/interop/manifest.json implementations/quicx.json
```

3. Build quicX image:
```bash
# The runner will build the image using the Dockerfile
# Make sure quicX repository is accessible or push to Docker Hub
docker tag quicx-interop:latest <your-dockerhub>/quicx-interop:latest
docker push <your-dockerhub>/quicx-interop:latest
```

4. Run tests:
```bash
# Test against other implementations
python3 run.py -s quicx -c quic-go,ngtcp2,mvfst,picoquic

# Test client mode
python3 run.py -s quic-go -c quicx

# Run specific test cases
python3 run.py -s quicx -c quicx -t handshake,transfer,http3
```

## Implementation Checklist

To fully support quic-interop-runner, implement the following:

### Core Functionality

- [x] Docker container setup
- [x] Endpoint script (server/client)
- [x] File serving (server)
- [x] File downloading (client)
- [ ] TLS certificate generation/handling (handled by quic-interop-runner)
- [ ] QLOG output support
- [ ] SSLKEYLOG support

### Test Cases

- [ ] `handshake` - Basic QUIC handshake
- [ ] `transfer` - File transfer (1MB, 10MB)
- [ ] `retry` - Retry packet handling
- [ ] `resumption` - Session resumption
- [ ] `zerortt` - 0-RTT data
- [ ] `http3` - HTTP/3 functionality
- [ ] `multiconnect` - Multiple connections
- [ ] `versionnegotiation` - Version negotiation
- [ ] `chacha20` - ChaCha20-Poly1305 cipher
- [ ] `keyupdate` - Key update
- [ ] `ecn` - ECN marking

### Advanced Features

- [ ] Connection migration
- [ ] NAT rebinding
- [ ] Amplification limit
- [ ] Flow control
- [ ] Stream limits

## Updating manifest.json

When adding support for new test cases, update `manifest.json`:

```json
{
  "test_cases": [
    "handshake",
    "transfer",
    "new_test_case"
  ]
}
```

## Debugging

### View Container Logs

```bash
docker logs <container-id>
```

### Enter Container

```bash
docker run -it --rm --entrypoint /bin/bash quicx-interop:latest
```

### Capture Packets

```bash
docker exec <container-id> tcpdump -i any -w /logs/capture.pcap udp port 443
```

### Analyze with Wireshark

```bash
# Copy capture file
docker cp <container-id>:/logs/capture.pcap .

# Open in Wireshark with TLS keys
wireshark capture.pcap -o tls.keylog_file:/logs/keys.log
```

## Continuous Integration

Add to your CI pipeline:

```yaml
# .github/workflows/interop.yml
name: QUIC Interop

on: [push, pull_request]

jobs:
  interop:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3

      - name: Build Docker image
        run: docker build -f interop/Dockerfile -t quicx-interop:latest .

      - name: Run interop tests
        run: ./interop/test.sh
```

## Resources

- [quic-interop-runner GitHub](https://github.com/marten-seemann/quic-interop-runner)
- [Interop Results Dashboard](https://interop.seemann.io/)
- [QUIC Implementations](https://github.com/quicwg/base-drafts/wiki/Implementations)
