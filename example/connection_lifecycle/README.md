# Connection Lifecycle Management Example

This example demonstrates best practices for managing HTTP/3 connection lifecycle.

## Features

- **Connection pooling** - Reuse connections efficiently
- **Health checks** - Monitor connection health
- **Graceful shutdown** - Clean connection termination
- **Connection reuse** - Maximize connection efficiency
- **Idle timeout handling** - Automatic cleanup of idle connections

## Building

```bash
cd build
cmake ..
make connection_lifecycle_demo
```

## Usage

### Demo Application

```bash
# Optional argument: base URL of a running HTTP/3 server (defaults to https://localhost:7004)
./bin/connection_lifecycle_demo [base_url]

# The demo will automatically:
# 1. Create a connection pool (max 5 connections per host, 30s idle timeout)
# 2. Make 5 requests reusing connections
# 3. Perform a health check (dump per-connection state)
# 4. Clean up idle connections
# 5. Demonstrate graceful shutdown
```

> Note: point the demo at any running HTTP/3 server (e.g. `hello_world_server` on port 7001)
> — it issues GET requests against `<base_url>/hello`.

## Key Concepts

All classes below are defined in `demo.cpp` for demonstration purposes (quicX itself
does not ship a built-in connection-pool API).

### 1. Connection Pooling

Reuse connections to avoid handshake overhead:

```cpp
ConnectionPool pool(5, 30000);  // max 5 connections per host, 30s idle timeout
auto conn = pool.GetConnection("https://example.com");
// Use connection
pool.ReleaseConnection("https://example.com", conn);
```

### 2. Health Checks

Dump per-connection state (in use / idle, idle time, request count):

```cpp
pool.HealthCheck();          // prints [HEALTHY] / [EXPIRED] per connection
pool.CleanupIdleConnections(); // removes idle-expired connections
```

### 3. Graceful Shutdown

Clean termination:

```cpp
pool.GracefulShutdown();  // Close all connections cleanly
```

## Best Practices

1. **Reuse connections** - Avoid creating new connections for each request
2. **Set appropriate timeouts** - Balance between keeping connections alive and resource usage
3. **Monitor health** - Detect and replace unhealthy connections
4. **Graceful shutdown** - Always cleanup properly
5. **Limit pool size** - Prevent resource exhaustion

## Output Example

```
╔════════════════════════════════════════╗
║  Connection Lifecycle Demo            ║
╚════════════════════════════════════════╝

Test 1: Connection Reuse
========================================

Request 1:
  Created new connection to https://localhost:7004
    Success: HTTP 200

Request 2:
  Reused existing connection (saved handshake time!)
    Total requests on this connection: 2
    Success: HTTP 200
...

Test 2: Health Check
========================================
  Host: https://localhost:7004
    Connection 0: IDLE, idle for 210ms, 5 requests [HEALTHY]

Test 3: Idle Connection Cleanup
========================================
Waiting 2 seconds...
Cleaning up idle connections...

Test 4: Graceful Shutdown
========================================
Initiating graceful shutdown...
  Closing 1 connections...
  All connections closed gracefully ✓

Demo completed!
```
