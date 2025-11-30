# quicX Interop Testing - Quick Start Guide

## 🚀 5-Minute Quick Start

### Step 1: Build Binaries (30 seconds)

```bash
cd /mnt/d/code/quicX/build
cmake --build . --target interop_server interop_client -j4
```

✅ Creates:
- `bin/interop_server` (28 MB)
- `bin/interop_client` (27 MB)

---

### Step 2: Generate Test Files (10 seconds)

```bash
mkdir -p ../interop/{www,downloads,logs,certs}
cd ../interop

# Test files
dd if=/dev/urandom of=www/1MB.bin bs=1M count=1
dd if=/dev/urandom of=www/10MB.bin bs=1M count=10
echo "Hello QUIC" > www/index.html

# Self-signed certificate
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout certs/key.pem -out certs/cert.pem \
  -days 365 -subj "/CN=localhost"
```

---

### Step 3: Run Server (background)

```bash
cd /mnt/d/code/quicX/build

# Terminal 1: Start server
./bin/interop_server
```

Server will listen on port 4433 (default).

---

### Step 4: Run Client (test)

```bash
# Terminal 2: Download file
cd /mnt/d/code/quicX/build

# Set environment
export SERVER=localhost
export PORT=4433
export REQUESTS="https://localhost:4433/1MB.bin"
export DOWNLOADS=../interop/downloads

./bin/interop_client
```

---

### Step 5: Verify Results

```bash
# Check downloaded file
ls -lh ../interop/downloads/

# Verify integrity
md5sum ../interop/www/1MB.bin ../interop/downloads/1MB.bin
```

---

## 🐳 Docker Quick Start

### Step 1: Build Image (2 minutes)

```bash
cd /mnt/d/code/quicX
docker build -f interop/Dockerfile -t quicx-interop:latest .
```

---

### Step 2: Run Tests (30 seconds)

```bash
./interop/test_all.sh
```

This runs:
- ✅ Handshake test
- ✅ Transfer 1MB test
- ✅ Transfer 10MB test
- ✅ HTTP/3 test
- ⚠️ ECN test (may fail)
- ⚠️ SSLKEYLOG test (may fail)

Expected: **4-6 tests pass**

---

## 📊 Test Against quic-interop-runner

### Setup (one-time)

```bash
# Clone runner
git clone https://github.com/marten-seemann/quic-interop-runner.git
cd quic-interop-runner

# Add quicX
cp /path/to/quicX/interop/manifest.json implementations/quicx.json

# Install dependencies
pip3 install -r requirements.txt
```

---

### Run Interop Tests

```bash
# Test quicX against itself
python3 run.py -s quicx -c quicx

# Test against quic-go
python3 run.py -s quicx -c quic-go

# Test specific scenarios
python3 run.py -s quicx -c quicx -t handshake,transfer,http3

# Full test suite
python3 run.py -s quicx -c quic-go,ngtcp2,mvfst,picoquic
```

---

## 🔧 Troubleshooting

### Server won't start

**Error**: "Failed to bind to port"

**Fix**:
```bash
# Check if port is in use
sudo lsof -i :4433

# Use different port
export PORT=8443
./bin/interop_server
```

---

### Client connection fails

**Error**: "Connection refused"

**Fix**:
```bash
# Check server is running
docker ps | grep quicx

# Check firewall
sudo ufw allow 4433/udp

# Try localhost instead of hostname
export SERVER=127.0.0.1
```

---

### File not downloaded

**Error**: "File not found: 404"

**Fix**:
```bash
# Check WWW directory
ls -la ../interop/www/

# Verify file exists
export WWW=/path/to/www
```

---

### Certificate errors

**Error**: "TLS handshake failed"

**Fix**:
```bash
# Regenerate certificate
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout certs/key.pem -out certs/cert.pem \
  -days 365 -subj "/CN=localhost"

# Check permissions
chmod 644 certs/cert.pem
chmod 600 certs/key.pem
```

---

## 📝 Environment Variables Cheat Sheet

### Minimal Server

```bash
ROLE=server
PORT=443
WWW=/www
```

### Minimal Client

```bash
ROLE=client
SERVER=localhost
PORT=443
REQUESTS="https://localhost:443/file.bin"
DOWNLOADS=/downloads
```

### Full Configuration

```bash
# Server
ROLE=server
PORT=443
WWW=/www
TESTCASE=transfer
QLOGDIR=/logs
SSLKEYLOGFILE=/logs/keys.log
ENABLE_ECN=1

# Client
ROLE=client
SERVER=server.example.com
PORT=443
REQUESTS="https://server.example.com:443/1MB.bin https://server.example.com:443/10MB.bin"
DOWNLOADS=/downloads
TESTCASE=transfer
QLOGDIR=/logs
SSLKEYLOGFILE=/logs/keys.log
ENABLE_ECN=1
```

---

## 🎯 Common Test Scenarios

### 1. Basic Handshake

```bash
# Server
TESTCASE=handshake ROLE=server PORT=4433 ./bin/interop_server

# Client
TESTCASE=handshake ROLE=client SERVER=localhost PORT=4433 \
  REQUESTS="https://localhost:4433/index.html" \
  ./bin/interop_client
```

---

### 2. Large File Transfer

```bash
# Server
TESTCASE=transfer ROLE=server PORT=4433 WWW=./www \
  ./bin/interop_server

# Client
TESTCASE=transfer ROLE=client SERVER=localhost PORT=4433 \
  REQUESTS="https://localhost:4433/10MB.bin" \
  DOWNLOADS=./downloads \
  ./bin/interop_client
```

---

### 3. Multiple Requests (HTTP/3)

```bash
# Server
TESTCASE=http3 ROLE=server PORT=4433 WWW=./www \
  ./bin/interop_server

# Client
TESTCASE=http3 ROLE=client SERVER=localhost PORT=4433 \
  REQUESTS="https://localhost:4433/file1.bin https://localhost:4433/file2.bin https://localhost:4433/file3.bin" \
  DOWNLOADS=./downloads \
  ./bin/interop_client
```

---

### 4. ECN Test

```bash
# Server
TESTCASE=ecn ROLE=server PORT=4433 ENABLE_ECN=1 \
  ./bin/interop_server

# Client
TESTCASE=ecn ROLE=client SERVER=localhost PORT=4433 \
  ENABLE_ECN=1 \
  REQUESTS="https://localhost:4433/1MB.bin" \
  ./bin/interop_client
```

---

### 5. Debug with SSLKEYLOG

```bash
# Server
SSLKEYLOGFILE=/tmp/server_keys.log ROLE=server PORT=4433 \
  ./bin/interop_server

# Client
SSLKEYLOGFILE=/tmp/client_keys.log ROLE=client SERVER=localhost PORT=4433 \
  REQUESTS="https://localhost:4433/1MB.bin" \
  ./bin/interop_client

# Capture packets
sudo tcpdump -i any -w /tmp/capture.pcap udp port 4433

# Decrypt in Wireshark
wireshark /tmp/capture.pcap -o tls.keylog_file:/tmp/client_keys.log
```

---

## 📚 Documentation Structure

```
/interop/
├── QUICKSTART.md         ← You are here (5-min guide)
├── README.md             ← Main documentation
├── INTEGRATION.md        ← quic-interop-runner integration
├── TEST_SCENARIOS.md     ← Detailed test descriptions
├── ADVANCED_FEATURES.md  ← Implementation guides
├── STATUS.md             ← Current implementation status
├── Dockerfile            ← Docker build
├── run_endpoint.sh       ← Entry point script
├── manifest.json         ← Interop metadata
├── test.sh               ← Basic tests
└── test_all.sh           ← Comprehensive tests
```

---

## ✅ Verification Checklist

Before running interop tests, verify:

- [ ] Binaries compiled: `ls -lh build/bin/interop_*`
- [ ] Test files exist: `ls -lh interop/www/*.bin`
- [ ] Certificates exist: `ls -lh interop/certs/*.pem`
- [ ] Port 4433 available: `sudo lsof -i :4433`
- [ ] Docker installed: `docker --version`
- [ ] Basic test passes: `./interop/test.sh`

---

## 🎓 Learning Path

1. **Day 1**: Run Quick Start (this guide)
2. **Day 2**: Read `README.md` for full features
3. **Day 3**: Study `TEST_SCENARIOS.md` for all tests
4. **Day 4**: Run `test_all.sh` and fix failures
5. **Day 5**: Integrate with `quic-interop-runner`
6. **Day 6**: Read `ADVANCED_FEATURES.md` for next features
7. **Day 7**: Implement QLOG or Retry

---

## 💡 Tips

### Performance

```bash
# Use more threads
Http3Config config;
config.thread_num_ = 8;  // Default: 4

# Increase stream limit
Http3Settings settings;
settings.max_concurrent_streams = 500;  // Default: 200
```

### Debugging

```bash
# Enable verbose logging
Http3Config config;
config.log_level_ = LogLevel::kDebug;  // Default: kInfo

# Capture all traffic
sudo tcpdump -i any -w capture.pcap udp port 4433
```

### Testing

```bash
# Test specific scenario
TESTCASE=handshake ./interop/test.sh

# Run only fast tests
./interop/test_all.sh | grep -E "handshake|transfer-1mb"

# Measure performance
time ./bin/interop_client
```

---

## 🔗 Quick Links

- Main Docs: `cat interop/README.md`
- Test Scenarios: `cat interop/TEST_SCENARIOS.md`
- Status: `cat interop/STATUS.md`
- Advanced: `cat interop/ADVANCED_FEATURES.md`

---

## ❓ FAQ

**Q: Do I need Docker?**
A: No, binaries work standalone. Docker is for official interop testing.

**Q: What ports are used?**
A: Default 4433/UDP (configurable via `PORT=<n>`).

**Q: Can I test over the internet?**
A: Yes, but you need a real certificate (not self-signed) and open UDP port.

**Q: How do I know if it works?**
A: Run `./interop/test_all.sh`. If ≥4 tests pass, it works.

**Q: What's the minimum to test?**
A: Handshake + Transfer. That proves QUIC works.

---

## 🎉 Success Criteria

You've successfully set up quicX interop if:

- ✅ Server starts without errors
- ✅ Client connects and downloads files
- ✅ Files are identical (checksum match)
- ✅ At least 4 tests pass in `test_all.sh`
- ✅ Docker image builds successfully

**Next**: Submit to https://interop.seemann.io/!
