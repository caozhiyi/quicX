# QUIC Interop Test Scenarios - Complete Implementation Guide

This document provides detailed implementation for all QUIC interoperability test cases defined by the [quic-interop-runner](https://github.com/marten-seemann/quic-interop-runner) framework.

## Table of Contents

1. [Core Test Cases](#core-test-cases)
2. [Advanced Test Cases](#advanced-test-cases)
3. [Implementation Status](#implementation-status)
4. [Configuration Options](#configuration-options)
5. [Testing Locally](#testing-locally)

---

## Core Test Cases

### 1. Handshake Test

**Purpose**: Verify basic QUIC handshake works correctly.

**Requirements**:
- Server must accept incoming connections
- Client must successfully connect
- TLS 1.3 handshake must complete
- Connection must be established

**Current Implementation**: ✅ Supported
- Server listens on specified port
- Client connects via URL
- Uses BoringSSL for TLS

**Test Command**:
```bash
# Server
TESTCASE=handshake ROLE=server PORT=443 ./bin/interop_server

# Client
TESTCASE=handshake ROLE=client SERVER=server PORT=443 \
  REQUESTS="https://server:443/index.html" ./bin/interop_client
```

---

### 2. Transfer Test

**Purpose**: Verify file transfer over QUIC works correctly for different file sizes.

**Requirements**:
- Server must serve files correctly
- Client must download files completely
- Support for 1MB, 10MB files
- Data integrity must be preserved

**Current Implementation**: ✅ Supported
- Server serves files from `/www` directory
- Client downloads to `/downloads` directory
- Binary file support

**Test Files Required**:
```bash
# Generate test files
dd if=/dev/urandom of=/www/1MB.bin bs=1M count=1
dd if=/dev/urandom of=/www/10MB.bin bs=1M count=10
```

**Test Command**:
```bash
# Server
TESTCASE=transfer ROLE=server PORT=443 WWW=/www ./bin/interop_server

# Client
TESTCASE=transfer ROLE=client SERVER=server PORT=443 \
  REQUESTS="https://server:443/1MB.bin https://server:443/10MB.bin" \
  DOWNLOADS=/downloads ./bin/interop_client
```

---

### 3. HTTP/3 Test

**Purpose**: Verify HTTP/3 protocol functionality.

**Requirements**:
- HTTP/3 framing must work
- QPACK header compression
- Multiple requests on same connection
- Stream multiplexing

**Current Implementation**: ✅ Supported (using quicX HTTP/3 library)
- HTTP/3 framing via quicX
- QPACK support
- Stream multiplexing

**Configuration**:
```cpp
Http3Settings settings;
settings.max_concurrent_streams = 200;
settings.max_frame_size = 16384;
settings.qpack_max_table_capacity = 4096;
```

**Test Command**:
```bash
# Server
TESTCASE=http3 ROLE=server PORT=443 WWW=/www ./bin/interop_server

# Client - multiple requests
TESTCASE=http3 ROLE=client SERVER=server PORT=443 \
  REQUESTS="https://server:443/file1.bin https://server:443/file2.bin https://server:443/file3.bin" \
  ./bin/interop_client
```

---

### 4. Retry Test

**Purpose**: Verify QUIC Retry mechanism for address validation.

**Requirements**:
- Server must send Retry packet on first connection attempt
- Client must respond with new Initial packet including Retry token
- Connection must establish after retry

**Implementation Needed**: ⚠️ Requires quicX API support

**Required API**:
```cpp
// Server config
Http3ServerConfig config;
config.config_.force_retry_ = true;  // Force retry for all connections

// Server should:
// 1. Receive Initial packet
// 2. Generate retry token
// 3. Send Retry packet
// 4. Validate token in subsequent Initial
```

**Test Command**:
```bash
# Server with retry enabled
TESTCASE=retry ROLE=server PORT=443 FORCE_RETRY=1 ./bin/interop_server

# Client (no special handling needed - automatic retry)
TESTCASE=retry ROLE=client SERVER=server PORT=443 \
  REQUESTS="https://server:443/1MB.bin" ./bin/interop_client
```

---

### 5. Resumption Test

**Purpose**: Verify TLS session resumption (1-RTT).

**Requirements**:
- First connection performs full handshake
- Session ticket is saved
- Second connection uses session ticket
- Second connection completes in 1-RTT

**Implementation Needed**: ⚠️ Requires session storage

**Required Implementation**:
```cpp
// Client side
class SessionCache {
public:
    void SaveSession(const std::string& server, const uint8_t* ticket, size_t len);
    bool LoadSession(const std::string& server, std::vector<uint8_t>& ticket);
};

// Server config
Http3ServerConfig config;
config.config_.enable_session_resumption_ = true;
config.config_.session_ticket_key_ = "...";  // 32-byte key
```

**Test Command**:
```bash
# First connection - full handshake
TESTCASE=resumption ROLE=client SERVER=server PORT=443 \
  REQUESTS="https://server:443/file1.bin" SESSION_CACHE=/tmp/session \
  ./bin/interop_client

# Second connection - resumed
TESTCASE=resumption ROLE=client SERVER=server PORT=443 \
  REQUESTS="https://server:443/file2.bin" SESSION_CACHE=/tmp/session \
  ./bin/interop_client
```

---

### 6. 0-RTT Test

**Purpose**: Verify 0-RTT data transmission.

**Requirements**:
- First connection establishes and saves session
- Second connection sends 0-RTT data
- Server accepts and processes 0-RTT data
- Full handshake completes after 0-RTT

**Implementation Needed**: ⚠️ Requires 0-RTT support

**Required Implementation**:
```cpp
// Server config
Http3ServerConfig config;
config.config_.enable_0rtt_ = true;
config.config_.max_early_data_size_ = 16384;

// Client
auto request = IRequest::Create();
request->SetEarlyData(true);  // Mark request for 0-RTT
client->DoRequest(url, HttpMethod::kGet, request, handler);
```

**Test Command**:
```bash
# Server with 0-RTT enabled
TESTCASE=zerortt ROLE=server PORT=443 ENABLE_0RTT=1 ./bin/interop_server

# Client - first connection
TESTCASE=zerortt ROLE=client SERVER=server PORT=443 \
  REQUESTS="https://server:443/file1.bin" SESSION_CACHE=/tmp/session \
  ./bin/interop_client

# Client - second connection with 0-RTT data
TESTCASE=zerortt ROLE=client SERVER=server PORT=443 \
  REQUESTS="https://server:443/file2.bin" SESSION_CACHE=/tmp/session \
  USE_0RTT=1 ./bin/interop_client
```

---

### 7. Multi-Connect Test

**Purpose**: Verify multiple simultaneous connections.

**Requirements**:
- Client opens multiple connections to server
- Each connection transfers data independently
- All connections succeed

**Current Implementation**: ✅ Supported (via multiple client instances)

**Test Command**:
```bash
# Server
TESTCASE=multiconnect ROLE=server PORT=443 WWW=/www ./bin/interop_server

# Client - run multiple instances in parallel
for i in {1..10}; do
  TESTCASE=multiconnect ROLE=client SERVER=server PORT=443 \
    REQUESTS="https://server:443/file${i}.bin" \
    ./bin/interop_client &
done
wait
```

---

### 8. Version Negotiation Test

**Purpose**: Verify QUIC version negotiation.

**Requirements**:
- Client proposes unsupported version
- Server responds with Version Negotiation packet
- Client retries with supported version
- Connection establishes

**Implementation Needed**: ⚠️ Requires version control API

**Required Implementation**:
```cpp
// Client config
Http3Config config;
config.supported_versions_ = {QUIC_VERSION_1, QUIC_VERSION_2};
config.preferred_version_ = QUIC_VERSION_UNSUPPORTED;  // Force negotiation

// Server config
Http3ServerConfig config;
config.config_.supported_versions_ = {QUIC_VERSION_1};
```

**Test Command**:
```bash
# Server supporting only v1
TESTCASE=versionnegotiation ROLE=server PORT=443 \
  QUIC_VERSIONS="0x00000001" ./bin/interop_server

# Client proposing unsupported version first
TESTCASE=versionnegotiation ROLE=client SERVER=server PORT=443 \
  REQUESTS="https://server:443/file.bin" \
  QUIC_VERSION_PROPOSE="0xff000099" ./bin/interop_client
```

---

### 9. ChaCha20 Test

**Purpose**: Verify ChaCha20-Poly1305 cipher support.

**Requirements**:
- Server supports ChaCha20-Poly1305
- Client negotiates ChaCha20 cipher
- Data transfer works with ChaCha20

**Current Implementation**: ⚠️ Depends on BoringSSL configuration

**Required Implementation**:
```cpp
// Server config
Http3ServerConfig config;
config.config_.cipher_suites_ = {"TLS_CHACHA20_POLY1305_SHA256"};

// Client verifies cipher in use
// Check via TLS handshake callback
```

**Test Command**:
```bash
# Server with ChaCha20
TESTCASE=chacha20 ROLE=server PORT=443 \
  CIPHER_SUITES="TLS_CHACHA20_POLY1305_SHA256" ./bin/interop_server

# Client
TESTCASE=chacha20 ROLE=client SERVER=server PORT=443 \
  REQUESTS="https://server:443/file.bin" \
  CIPHER_SUITES="TLS_CHACHA20_POLY1305_SHA256" ./bin/interop_client
```

---

### 10. Key Update Test

**Purpose**: Verify TLS key update mechanism.

**Requirements**:
- Connection establishes normally
- Mid-transfer, one side initiates key update
- Other side acknowledges and updates keys
- Transfer continues successfully

**Implementation Needed**: ⚠️ Requires key update API

**Required Implementation**:
```cpp
// Trigger key update after N bytes
class KeyUpdateTrigger {
public:
    void OnDataSent(size_t bytes) {
        if (bytes > trigger_threshold_) {
            connection->UpdateKeys();
        }
    }
};

// API needed
interface IQuicConnection {
    bool UpdateKeys();  // Trigger key update
};
```

**Test Command**:
```bash
# Server
TESTCASE=keyupdate ROLE=server PORT=443 \
  KEY_UPDATE_BYTES=1048576 ./bin/interop_server  # Update after 1MB

# Client
TESTCASE=keyupdate ROLE=client SERVER=server PORT=443 \
  REQUESTS="https://server:443/10MB.bin" ./bin/interop_client
```

---

### 11. ECN Test

**Purpose**: Verify ECN (Explicit Congestion Notification) marking.

**Requirements**:
- Packets sent with ECN markings (ECT(0), ECT(1))
- ECN feedback in ACK frames
- Congestion response to ECN-CE marks

**Current Implementation**: ⚠️ Partial (enable_ecn flag exists)

**Required Implementation**:
```cpp
// Server config
Http3Config config;
config.enable_ecn_ = true;

// Socket must set ECN bits on outgoing packets
// Monitor ECN feedback in ACK frames
```

**Test Command**:
```bash
# Server with ECN
TESTCASE=ecn ROLE=server PORT=443 ENABLE_ECN=1 ./bin/interop_server

# Client with ECN
TESTCASE=ecn ROLE=client SERVER=server PORT=443 \
  REQUESTS="https://server:443/10MB.bin" ENABLE_ECN=1 ./bin/interop_client
```

---

## Advanced Test Cases

### 12. Connection Migration Test

**Purpose**: Verify connection migration when IP address changes.

**Requirements**:
- Connection established on IP1
- Client switches to IP2
- Client sends PATH_CHALLENGE on new path
- Server validates new path with PATH_RESPONSE
- Connection continues on new path

**Implementation Needed**: ⚠️ Requires migration API

**Required Implementation**:
```cpp
// Client API
interface IQuicConnection {
    bool MigrateTo(const std::string& new_local_addr);
};

// Server must:
// 1. Receive packet from new address
// 2. Send PATH_CHALLENGE
// 3. Validate PATH_RESPONSE
// 4. Update connection path
```

---

### 13. NAT Rebinding Test

**Purpose**: Verify NAT rebinding handling (port change, same IP).

**Requirements**:
- Connection established through NAT
- NAT rebinds to new port
- Connection continues without interruption

**Implementation**: Similar to migration, but only port changes

---

### 14. Amplification Limit Test

**Purpose**: Verify amplification attack mitigation.

**Requirements**:
- Server limits response to 3x received bytes before address validation
- Server uses Retry or PATH_CHALLENGE for validation
- After validation, normal transmission resumes

**Implementation Needed**: ⚠️ Requires amplification tracking

---

### 15. Flow Control Test

**Purpose**: Verify stream and connection flow control.

**Requirements**:
- Sender respects stream flow control limits
- Sender respects connection flow control limits
- MAX_STREAM_DATA updates allow more data
- MAX_DATA updates allow more data

**Current Implementation**: ✅ Likely supported (check quicX flow control)

---

### 16. Stream Limits Test

**Purpose**: Verify stream limit enforcement.

**Requirements**:
- Server enforces max_concurrent_streams limit
- Client cannot exceed stream limit
- STREAMS_BLOCKED frame sent when limit reached
- MAX_STREAMS update allows new streams

**Current Implementation**: ⚠️ Partial (max_concurrent_streams = 200)

---

## Implementation Status

| Test Case | Status | Notes |
|-----------|--------|-------|
| Handshake | ✅ Complete | Works with current implementation |
| Transfer | ✅ Complete | 1MB, 10MB files supported |
| HTTP/3 | ✅ Complete | Full HTTP/3 support |
| Retry | ⚠️ Partial | Need force_retry config |
| Resumption | ⚠️ Partial | Need session cache |
| 0-RTT | ⚠️ Partial | Need 0-RTT API |
| Multi-Connect | ✅ Complete | Multiple instances |
| Version Negotiation | ⚠️ Missing | Need version API |
| ChaCha20 | ⚠️ Unknown | Depends on BoringSSL |
| Key Update | ⚠️ Missing | Need key update API |
| ECN | ⚠️ Partial | Flag exists, need testing |
| Connection Migration | ⚠️ Missing | Need migration API |
| NAT Rebinding | ⚠️ Missing | Similar to migration |
| Amplification Limit | ⚠️ Unknown | Need verification |
| Flow Control | ✅ Likely | Check implementation |
| Stream Limits | ✅ Likely | Check implementation |

---

## Configuration Options

### Environment Variables

All test scenarios support these environment variables:

```bash
# Required
ROLE=server|client        # Endpoint role
TESTCASE=<name>           # Test scenario name

# Server specific
PORT=443                  # Listen port
WWW=/www                  # Document root
CERT=/certs/cert.pem     # TLS certificate
KEY=/certs/key.pem       # TLS private key

# Client specific
SERVER=<hostname>         # Server hostname
REQUESTS="<url1> <url2>"  # URLs to download
DOWNLOADS=/downloads      # Download directory

# Optional
QLOGDIR=/logs             # QLOG output directory
SSLKEYLOGFILE=/logs/keys  # TLS key log file
ENABLE_ECN=1              # Enable ECN
FORCE_RETRY=1             # Force retry
ENABLE_0RTT=1             # Enable 0-RTT
SESSION_CACHE=/tmp/sess   # Session cache file
```

### Code Configuration

```cpp
// HTTP/3 Settings
Http3Settings settings;
settings.max_concurrent_streams = 200;      // Max streams per connection
settings.max_frame_size = 16384;           // Max HTTP/3 frame size
settings.max_field_section_size = 16384;   // Max header section size
settings.qpack_max_table_capacity = 4096;  // QPACK table size
settings.enable_push = 0;                  // Server push disabled

// HTTP/3 Config
Http3Config config;
config.thread_num_ = 4;                    // I/O threads
config.log_level_ = LogLevel::kInfo;       // Logging
config.enable_ecn_ = true;                 // ECN support
config.connection_timeout_ms_ = 30000;     // 30s timeout
```

---

## Testing Locally

### 1. Generate Test Files

```bash
mkdir -p /tmp/www /tmp/downloads /tmp/logs /tmp/certs

# Test files
dd if=/dev/urandom of=/tmp/www/1MB.bin bs=1M count=1
dd if=/dev/urandom of=/tmp/www/10MB.bin bs=1M count=10
dd if=/dev/urandom of=/tmp/www/index.html bs=1K count=1

# Self-signed certificate
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout /tmp/certs/key.pem \
  -out /tmp/certs/cert.pem \
  -days 365 -subj "/CN=localhost"
```

### 2. Run Server

```bash
cd /mnt/d/code/quicX/build

# Basic server
ROLE=server PORT=4433 WWW=/tmp/www \
  ./bin/interop_server

# Or with specific test case
TESTCASE=transfer ROLE=server PORT=4433 WWW=/tmp/www \
  ./bin/interop_server
```

### 3. Run Client

```bash
# Basic client
ROLE=client SERVER=localhost PORT=4433 \
  REQUESTS="https://localhost:4433/1MB.bin" \
  DOWNLOADS=/tmp/downloads \
  ./bin/interop_client

# Or with test case
TESTCASE=transfer ROLE=client SERVER=localhost PORT=4433 \
  REQUESTS="https://localhost:4433/1MB.bin https://localhost:4433/10MB.bin" \
  DOWNLOADS=/tmp/downloads \
  ./bin/interop_client
```

### 4. Verify Results

```bash
# Check downloaded files
ls -lh /tmp/downloads/

# Compare checksums
md5sum /tmp/www/1MB.bin /tmp/downloads/1MB.bin
```

---

## QLOG and SSLKEYLOG Support

### QLOG Implementation

QLOG provides detailed logging of QUIC events for debugging.

**Required Implementation**:
```cpp
class QlogWriter {
public:
    void LogPacketSent(const QuicPacket& packet);
    void LogPacketReceived(const QuicPacket& packet);
    void LogFrameProcessed(const QuicFrame& frame);
    void LogConnectionState(const ConnectionState& state);

    void FlushToFile(const std::string& filepath);
};

// Usage
if (qlog_dir) {
    std::string qlog_path = qlog_dir + "/connection_" + conn_id + ".qlog";
    qlog_writer_.FlushToFile(qlog_path);
}
```

**QLOG Format**: JSON-based, see [qlog schema](https://github.com/quiclog/qlog)

### SSLKEYLOG Implementation

SSLKEYLOG allows decryption of captured packets in Wireshark.

**Required Implementation**:
```cpp
// BoringSSL callback
void ssl_keylog_callback(const SSL* ssl, const char* line) {
    if (keylog_file_) {
        fprintf(keylog_file_, "%s\n", line);
        fflush(keylog_file_);
    }
}

// Setup
if (keylog_path) {
    keylog_file_ = fopen(keylog_path, "a");
    SSL_CTX_set_keylog_callback(ssl_ctx_, ssl_keylog_callback);
}
```

**Format**: NSS Key Log Format
```
CLIENT_RANDOM <64 hex chars> <96 hex chars>
SERVER_HANDSHAKE_TRAFFIC_SECRET <64 hex> <64 hex>
EXPORTER_SECRET <64 hex> <64 hex>
...
```

---

## Next Steps

### High Priority
1. ✅ Implement basic file transfer (DONE)
2. ⚠️ Add QLOG support
3. ⚠️ Add SSLKEYLOG support
4. ⚠️ Add Retry support
5. ⚠️ Add session resumption

### Medium Priority
6. ⚠️ Add 0-RTT support
7. ⚠️ Test ECN functionality
8. ⚠️ Add key update
9. ⚠️ Verify flow control

### Low Priority
10. ⚠️ Connection migration
11. ⚠️ NAT rebinding
12. ⚠️ Version negotiation

### Testing
- Run full interop test suite
- Test against other implementations (quic-go, ngtcp2, etc.)
- Submit results to https://interop.seemann.io/
