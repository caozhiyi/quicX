# QUIC Interop Testing - Quick Reference Card

> Quick command reference for quicX QUIC interoperability testing

---

## 🚀 Quick Start

```bash
# Build Docker image
docker build -f test/interop/Dockerfile -t quicx-interop:latest ../..

# Run all tests
cd test/interop
./run_full_interop.sh

# Run specific scenario
./run_full_interop.sh --scenario handshake
```

---

## 📋 All 14 Test Scenarios

| Scenario | Command | Status |
|----------|---------|--------|
| handshake | `./run_full_interop.sh --scenario handshake` | ✅ Ready |
| transfer | `./run_full_interop.sh --scenario transfer` | ✅ Ready |
| retry | `./run_full_interop.sh --scenario retry` | 🚧 WIP |
| resumption | `./run_full_interop.sh --scenario resumption` | ✅ Ready |
| zerortt | `./run_full_interop.sh --scenario zerortt` | ✅ Ready |
| http3 | `./run_full_interop.sh --scenario http3` | ✅ Ready |
| multiconnect | `./run_full_interop.sh --scenario multiconnect` | ✅ Ready |
| versionnegotiation | `./run_full_interop.sh --scenario versionnegotiation` | 🚧 WIP |
| chacha20 | `./run_full_interop.sh --scenario chacha20` | ✅ Ready |
| keyupdate | `./run_full_interop.sh --scenario keyupdate` | 🚧 WIP |
| v2 | `./run_full_interop.sh --scenario v2` | 📋 Planned |
| rebind-port | `./run_full_interop.sh --scenario rebind-port` | 📋 Planned |
| rebind-addr | `./run_full_interop.sh --scenario rebind-addr` | 📋 Planned |
| connectionmigration | `./run_full_interop.sh --scenario connectionmigration` | 📋 Planned |

---

## 📊 Output Formats

```bash
# Text output (default, colored)
./run_full_interop.sh

# JSON output
./run_full_interop.sh --output json > results.json

# Markdown output
./run_full_interop.sh --output markdown > results.md

# Plain text (no colors)
./run_full_interop.sh --no-color
```

---

## 🔄 Cross-Implementation Testing

```bash
# Test against default implementations
./cross_impl_matrix.sh

# Custom implementations
./cross_impl_matrix.sh --implementations quicx,quiche,ngtcp2,quic-go

# Generate HTML matrix
./cross_impl_matrix.sh --format html --output matrix.html

# All formats
./cross_impl_matrix.sh --format all
```

---

## 🐛 Quick Troubleshooting

```bash
# Check Docker image
docker images | grep quicx-interop

# View server logs
docker logs quicx-server

# Clean up containers
docker stop $(docker ps -aq) && docker rm $(docker ps -aq)

# Regenerate test files
rm -rf www/*.bin && ./run_full_interop.sh

# Check port availability
lsof -i :4433
```

---

## 📁 Test Files

| File | Size | Used In |
|------|------|---------|
| 1KB.bin | 1 KB | handshake, retry, resumption, zerortt, v2, versionnegotiation |
| 10KB.bin | 10 KB | http3 |
| 100KB.bin | 100 KB | http3 |
| 1MB.bin | 1 MB | transfer, http3 |
| 2MB.bin | 2 MB | keyupdate |
| 5MB.bin | 5 MB | transfer, chacha20, rebind-*, connectionmigration |

---

## 🔧 Common Commands

```bash
# Full test suite
./run_full_interop.sh

# Single scenario
./run_full_interop.sh --scenario <name>

# JSON report
./run_full_interop.sh --output json

# Cross-impl matrix (3 impls)
./cross_impl_matrix.sh --implementations quicx,quiche,ngtcp2

# HTML matrix report
./cross_impl_matrix.sh --format html

# Help
./run_full_interop.sh --help
./cross_impl_matrix.sh --help
```

---

## 📖 Full Documentation

See [INTEROP_COMPLETE_GUIDE.md](./INTEROP_COMPLETE_GUIDE.md) for detailed documentation.

---

## 🔗 Quick Links

- **QUIC Interop Runner:** https://github.com/quic-interop/quic-interop-runner
- **Interop Results:** https://interop.seemann.io/
- **RFC 9000 (QUIC):** https://www.rfc-editor.org/rfc/rfc9000.html
- **RFC 9114 (HTTP/3):** https://www.rfc-editor.org/rfc/rfc9114.html

---

*quicX QUIC Interoperability Test Suite v2.0*
