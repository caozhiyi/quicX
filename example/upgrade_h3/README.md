# HTTP/3 Upgrade Example

This example demonstrates how to use the `quicX::upgrade` module to handle protocol negotiation and transparently upgrade clients to HTTP/3.

## Overview

The `upgrade_h3` server listens for incoming connections and advertises HTTP/3 support via the `Alt-Svc` header (for HTTP/1.1 and HTTP/2) or ALPN negotiation. This allows clients to discover and switch to the more efficient QUIC/HTTP/3 protocol.

## Features Demonstrated

- **Multi-Protocol Support**: Simultaneous support for HTTP/1.1, HTTP/2, and HTTP/3.
- **Protocol Advertisement**: Automatically adds `Alt-Svc` headers to responses to inform clients about HTTP/3 availability.
- **Unified Event Loop**: Uses `quicx::common::EventLoop` for efficient I/O handling.

## Build

### Using CMake (from project root)

```bash
# Configure
mkdir -p build && cd build
cmake ..

# Build
make upgrade_h3_server

# Or build all examples
make
```

The executable will be in `build/bin/`:
- `upgrade_h3_server`

## Usage

### Start the Server

```bash
./build/bin/upgrade_h3_server
```

The server listens on:
- **HTTP**: Port 8080 (for cleartext/h2c)
- **HTTPS/HTTP3**: Port 8443 (if certificates are configured)

Output:
```
upgrade_h3_server running on 0.0.0.0:8080, advertising h3 on :8443
```

### Testing with Curl

You can check if the upgrade is working by sending a request with a client that supports HTTP/3 (like modern `curl` or a browser).

```bash
# Verify Alt-Svc header is present
curl -v http://localhost:8080
```

Look for the `Alt-Svc` header in the response:
```
< Alt-Svc: h3=":8443"; ma=2592000
```

## Implementation Details

The `IUpgrade` interface simplifies the process of setting up a multi-protocol server:

```cpp
UpgradeSettings settings;
settings.http_port = 8080;
settings.h3_port = 8443;
settings.enable_http1 = true;
settings.enable_http2 = true;
settings.enable_http3 = true;

// Create server and add listener
auto server = IUpgrade::MakeUpgrade(event_loop);
server->AddListener(settings);
```
