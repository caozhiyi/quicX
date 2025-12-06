# QUIC Interoperability Testing

Official interoperability test suite for quicX, designed to verify compatibility with other QUIC implementations according to the [QUIC Interop Runner](https://github.com/quic-interop/quic-interop-runner) standard.

## 🎯 Purpose

This directory contains **comprehensive interoperability tests** that validate:
- ✅ quicX complies with QUIC/HTTP/3 RFCs (RFC 9000, 9001, 9114, 9204)
- ✅ quicX works with other implementations (quiche, ngtcp2, quic-go, etc.)
- ✅ All 14+ official QUIC Interop Runner test scenarios
- ✅ Cross-implementation compatibility matrix generation

**This is NOT for quicX self-testing** - use `unit_test/` and `test/integration/` for that.

---

## 🚀 Quick Start

### 1. Build Docker Image

```bash
cd /path/to/quicX
docker build -f test/interop/Dockerfile -t quicx-interop:latest .
```

### 2. Run Complete Test Suite

```bash
cd test/interop

# Run all 14 test scenarios
./run_full_interop.sh

# Or run specific scenario
./run_full_interop.sh --scenario handshake
```

### 3. Generate Compatibility Matrix

```bash
# Test against other QUIC implementations
./cross_impl_matrix.sh --implementations quicx,quiche,ngtcp2

# Generate HTML report
./cross_impl_matrix.sh --format html --output matrix.html
```

---

## 📋 Test Scripts

| Script | Purpose | Scenarios | Run Time |
|--------|---------|-----------|----------|
| **`run_full_interop.sh`** ⭐ | Complete official test suite | 14+ | ~5 min |
| **`cross_impl_matrix.sh`** ⭐ | Cross-implementation matrix | 7 core | ~10-30 min |

⭐ Both scripts support multiple output formats and comprehensive reporting

---

## 🧪 Supported Test Scenarios

### Official QUIC Interop Runner Scenarios (14+)

| # | Scenario | Description | quicX Status |
|---|----------|-------------|--------------|
| 1 | **handshake** | Basic QUIC handshake | ✅ Ready |
| 2 | **transfer** | Flow control & multiplexing | ✅ Ready |
| 3 | **retry** | Retry packet handling | 🚧 Implementing |
| 4 | **resumption** | Session resumption (1-RTT) | ✅ Ready |
| 5 | **zerortt** | 0-RTT early data | ✅ Ready |
| 6 | **http3** | HTTP/3 functionality | ✅ Ready |
| 7 | **multiconnect** | Handshake under packet loss | ✅ Ready |
| 8 | **versionnegotiation** | Version negotiation | 🚧 Implementing |
| 9 | **chacha20** | ChaCha20-Poly1305 cipher | ✅ Ready |
| 10 | **keyupdate** | TLS key update | 🚧 Implementing |
| 11 | **v2** | QUIC version 2 | 📋 Planned |
| 12 | **rebind-port** | NAT port rebinding | 📋 Planned |
| 13 | **rebind-addr** | NAT address rebinding | 📋 Planned |
| 14 | **connectionmigration** | Connection migration | 📋 Planned |

**Current Support:** 7/14 scenarios fully ready

---

## 📖 Documentation

### Quick Start
- **[QUICK_REFERENCE.md](./QUICK_REFERENCE.md)** - Command cheat sheet

### Comprehensive Guides
- **[INTEROP_COMPLETE_GUIDE.md](./INTEROP_COMPLETE_GUIDE.md)** - Complete testing guide
- **[TEST_SCENARIOS.md](./TEST_SCENARIOS.md)** - Detailed test scenario specifications

### Implementation Guides
- **[IMPLEMENTATION_PLAN.md](./IMPLEMENTATION_PLAN.md)** - Development roadmap
- **[RETRY_PRODUCTION_GUIDE.md](./RETRY_PRODUCTION_GUIDE.md)** - Retry mechanism implementation
- **[RESUMPTION_0RTT_VN_TESTS.md](./RESUMPTION_0RTT_VN_TESTS.md)** - Advanced features (0-RTT, resumption, version negotiation)

---

## 🎯 Usage Examples

### Example 1: Run Complete Test Suite

```bash
# All 14 official scenarios with colored output
./run_full_interop.sh

# Generate JSON report
./run_full_interop.sh --output json > results.json

# Generate Markdown documentation
./run_full_interop.sh --output markdown > TEST_RESULTS.md
```

### Example 2: Test Specific Scenarios

```bash
# Test only handshake
./run_full_interop.sh --scenario handshake

# Test HTTP/3
./run_full_interop.sh --scenario http3

# Test 0-RTT
./run_full_interop.sh --scenario zerortt
```

### Example 3: Cross-Implementation Matrix

```bash
# Test against 3 implementations
./cross_impl_matrix.sh --implementations quicx,quiche,ngtcp2

# Generate HTML report
./cross_impl_matrix.sh --format html --output matrix.html

# Generate all formats (Markdown, HTML, JSON)
./cross_impl_matrix.sh --format all
```

### Example 4: Help and Options

```bash
# Show available options
./run_full_interop.sh --help
./cross_impl_matrix.sh --help

# Disable colored output
./run_full_interop.sh --no-color
```

---

## 📊 Output Formats

### Text Output (Default)
Human-readable colored terminal output with test results and summary.

### JSON Output
Machine-readable format for automated processing and CI/CD integration.

```bash
./run_full_interop.sh --output json > results.json
```

### Markdown Output
Documentation-ready format for GitHub and reports.

```bash
./run_full_interop.sh --output markdown > RESULTS.md
```

### HTML Output (Matrix only)
Interactive web report with color-coded results and professional styling.

```bash
./cross_impl_matrix.sh --format html --output matrix.html
```

---

## 🔧 Core Components

### Directory Structure

```
test/interop/
├── run_full_interop.sh         # Main test suite (14+ scenarios)
├── cross_impl_matrix.sh        # Cross-implementation matrix generator
├── interop_server.cpp          # Test server implementation
├── interop_client.cpp          # Test client implementation
├── run_endpoint.sh             # Container entrypoint script
├── Dockerfile                  # Docker image configuration
├── docker-compose.yml          # Local test environment
├── manifest.json               # Interop runner metadata
├── CMakeLists.txt              # Build configuration
├── www/                        # Test files (auto-generated)
├── downloads/                  # Downloaded files
└── logs/                       # qlog and TLS key logs
    ├── server/
    └── client/
```

### Test Files

Standard test files in `www/`:

| File | Size | Used In |
|------|------|---------|
| 32B.bin | 32 bytes | Minimal transfer |
| 1KB.bin | 1 KB | handshake, retry, resumption, zerortt |
| 5KB.bin | 5 KB | Small transfer |
| 10KB.bin | 10 KB | http3 |
| 100KB.bin | 100 KB | http3 |
| 500KB.bin | 500 KB | Medium transfer |
| 1MB.bin | 1 MB | transfer, http3 |
| 2MB.bin | 2 MB | keyupdate |
| 3MB.bin | 3 MB | Large transfer |
| 5MB.bin | 5 MB | transfer, chacha20, rebind-* |
| 10MB.bin | 10 MB | Extra large transfer |

Files are automatically generated if missing.

---

## 🐛 Debugging

### View Logs

```bash
# Server logs
docker logs quicx-server

# qlog files
ls -la logs/server/
ls -la logs/client/

# TLS key logs
cat logs/server/keys.log
```

### Manual Testing

```bash
# Start server manually
docker run -d --rm \
  --name quicx-server \
  --network host \
  -e ROLE=server -e PORT=4433 -e WWW=/www \
  -v "$PWD/www:/www:ro" \
  quicx-interop:latest

# Run client manually
docker run --rm \
  --network host \
  -e ROLE=client \
  -e SERVER=localhost -e PORT=4433 \
  -e REQUESTS="https://localhost:4433/1MB.bin" \
  -v "$PWD/downloads:/downloads" \
  quicx-interop:latest

# Verify file
cmp www/1MB.bin downloads/1MB.bin && echo "✓ PASS"

# Cleanup
docker stop quicx-server
```

### Common Issues

**Connection timeout:**
```bash
# Check server running
docker ps | grep quicx-server

# Check UDP port
docker exec quicx-server netstat -uln | grep 4433
```

**File integrity fail:**
```bash
# Check SHA-256
sha256sum www/1MB.bin downloads/1MB.bin

# Regenerate test files
rm -f www/*.bin
./run_full_interop.sh  # Auto-generates files
```

**Docker image not found:**
```bash
# Rebuild
docker build -f Dockerfile -t quicx-interop:latest ../..
```

---

## 🔗 Integration

### CI/CD Example

```yaml
name: Interop Tests

on: [push, pull_request]

jobs:
  interop:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3

      - name: Build Docker Image
        run: docker build -f test/interop/Dockerfile -t quicx-interop:latest .

      - name: Run Interop Tests
        run: |
          cd test/interop
          ./run_full_interop.sh --output json > results.json

      - name: Upload Results
        if: always()
        uses: actions/upload-artifact@v3
        with:
          name: interop-results
          path: test/interop/results.json

      - name: Upload Logs
        if: always()
        uses: actions/upload-artifact@v3
        with:
          name: interop-logs
          path: test/interop/logs/
```

### Official Interop Runner

To submit quicX to https://interop.seemann.io/:

1. Ensure all tests pass
2. Update `manifest.json` with supported scenarios
3. Submit PR to [quic-interop-runner](https://github.com/quic-interop/quic-interop-runner)

---

## 📈 Roadmap

### Current Status: 7/14 Scenarios Ready

**Fully Implemented:**
- ✅ handshake - Basic connectivity
- ✅ transfer - Data reliability
- ✅ resumption - Session resumption (already implemented!)
- ✅ zerortt - 0-RTT support (already implemented!)
- ✅ http3 - HTTP/3 functionality
- ✅ multiconnect - Concurrent connections
- ✅ chacha20 - ChaCha20 cipher (already implemented!)

**In Progress:**
- 🚧 retry - Retry packet handling (needs RetryTokenManager)
- 🚧 versionnegotiation - Version negotiation (needs client config)
- 🚧 keyupdate - TLS key update

**Planned:**
- 📋 v2 - QUIC version 2
- 📋 rebind-port - NAT port rebinding
- 📋 rebind-addr - NAT address rebinding
- 📋 connectionmigration - Connection migration

**Goal:** Achieve 14/14 tests passing!

---

## 🔗 Cross-Implementation Support

### Supported Implementations

| Implementation | Maintainer | Docker Image |
|----------------|------------|--------------|
| quicX | This project | quicx-interop:latest |
| quiche | Cloudflare | cloudflare/quiche:latest |
| ngtcp2 | nghttp2 | ngtcp2/ngtcp2:latest |
| quic-go | Go | martenseemann/quic-go-interop:latest |
| mvfst | Meta | mvfst/mvfst-qns:latest |
| quinn | Rust | quinn-rs/quinn-interop:latest |
| aioquic | Python | aiortc/aioquic:latest |
| picoquic | C (minimal) | private-octopus/picoquic:latest |
| neqo | Mozilla | mozilla/neqo-qns:latest |
| lsquic | LiteSpeed | litespeedtech/lsquic-qir:latest |

---

## 📚 References

### RFCs
- [QUIC RFC 9000](https://www.rfc-editor.org/rfc/rfc9000.html) - Transport Protocol
- [QUIC-TLS RFC 9001](https://www.rfc-editor.org/rfc/rfc9001.html) - TLS Integration
- [HTTP/3 RFC 9114](https://www.rfc-editor.org/rfc/rfc9114.html) - HTTP over QUIC
- [QPACK RFC 9204](https://www.rfc-editor.org/rfc/rfc9204.html) - Header Compression

### Tools
- [QUIC Interop Runner](https://github.com/quic-interop/quic-interop-runner) - Official test framework
- [Public Interop Results](https://interop.seemann.io/) - Live test results
- [IETF QUIC WG](https://datatracker.ietf.org/wg/quic/about/) - Working group

---

## 🙋 FAQ

**Q: How many test scenarios are supported?**
A: 14+ official QUIC Interop Runner scenarios. Currently 7/14 are fully implemented.

**Q: What's the difference between the two test scripts?**
A:
- `run_full_interop.sh` - Tests quicX against itself (all 14 scenarios)
- `cross_impl_matrix.sh` - Tests quicX against other QUIC implementations

**Q: Which output format should I use?**
A:
- **Text** - For terminal viewing and quick checks
- **JSON** - For CI/CD and automated processing
- **Markdown** - For documentation and reports
- **HTML** - For visual matrix reports (cross-impl only)

**Q: Can I test against other implementations without Docker Hub?**
A: Yes, use `run_full_interop.sh` which only needs the local quicX image.

**Q: How long does testing take?**
- Full test suite (14 scenarios): ~5 minutes
- Cross-implementation (3 impls): ~10-15 minutes
- Single scenario: ~30 seconds

**Q: Why are some tests marked as "UNSUPPORTED"?**
A: These features are not yet implemented in quicX. See the roadmap for planned work.

---

**For detailed documentation, see:**
- [INTEROP_COMPLETE_GUIDE.md](./INTEROP_COMPLETE_GUIDE.md) - Comprehensive guide
- [QUICK_REFERENCE.md](./QUICK_REFERENCE.md) - Quick command reference
- [TEST_SCENARIOS.md](./TEST_SCENARIOS.md) - Scenario specifications

---

*quicX QUIC Interoperability Test Suite v2.0*
*Compatible with [QUIC Interop Runner](https://github.com/quic-interop/quic-interop-runner)*
