<p align="left"><img width="500" src="./docs/image/logo.png" alt="cppnet logo"></p>

<p align="left">
    <a href="https://opensource.org/licenses/BSD-3-Clause"><img src="https://img.shields.io/badge/license-bsd-orange.svg" alt="Licenses"></a>
</p> 


QuicX is a high-performance HTTP/3 library based on the QUIC protocol (RFC 9000), implemented in C++11. This project provides a complete QUIC and HTTP/3 network transmission solution, supporting high concurrency and low latency network communication needs.

## Features

### QUIC Protocol Implementation
- **Connection Management**
  - TLS 1.3 encryption and secure handshake based on BoringSSL
  - Support for 0-RTT and 1-RTT connection establishment
  - Connection migration support
  - Graceful connection closure handling

- **Flow Control**
  - Window-based flow control
  - Stream-level and connection-level concurrency control
  - Support for STREAM_DATA_BLOCKED and DATA_BLOCKED frames

- **Congestion Control**
  - Implementation of multiple congestion control algorithms:
    - BBR
    - Cubic
    - Reno
  - Intelligent packet retransmission strategy

- **Data Transmission**
  - Reliable packet transmission and acknowledgment mechanism
  - Efficient frame packaging and parsing
  - Support for PING/PONG heartbeat detection
  - PATH_CHALLENGE/PATH_RESPONSE path validation

### HTTP/3 Implementation
- **Header Compression**
  - Implementation of QPACK dynamic and static tables
  - Efficient Huffman coding
  - Header field validation and processing

- **Stream Management**
  - Support for bidirectional and unidirectional streams
  - Stream priority handling
  - Flow control and backpressure handling

- **Request Handling**
  - Support for all HTTP methods
  - Flexible routing system
  - Middleware mechanism
  - Complete lifecycle management of requests and responses

### Core Components

- **Memory Management**
  - Efficient memory pool implementation
  - Intelligent buffer management
  - Zero-copy data transmission optimization

- **Concurrency Control**
  - Thread-safe data structures
  - Lock-free queue implementation
  - Efficient event loop

- **Logging System**
  - Multi-level logging support
  - Configurable log output
  - Performance statistics and monitoring

## Quick Start

### Build Requirements
- C++11 or higher
- BoringSSL
- GTest (optional, for unit testing)

### Build Steps
```bash
git clone https://github.com/caozhiyi/quicX.git
cd quicX
make
```

## Performance Optimization

### Memory Management
- Use memory pool to reduce memory allocation overhead
- Intelligent buffer management to avoid frequent memory copying
- Implement zero-copy mechanism to improve data transmission efficiency

### Concurrency Handling
- Multi-threaded event loop for network I/O processing
- Lock-free data structures to reduce thread contention
- Efficient task scheduling mechanism

### Network Optimization
- Intelligent packet retransmission strategy
- Adaptive congestion control
- Efficient flow control algorithms

## License
MIT License - See the [LICENSE](LICENSE) file for more information.
