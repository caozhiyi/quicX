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
./bin/connection_lifecycle_demo

# The demo will automatically:
# 1. Create a connection pool
# 2. Make multiple requests reusing connections
# 3. Perform health checks
# 4. Demonstrate graceful shutdown
```

## Key Concepts

### 1. Connection Pooling

Reuse connections to avoid handshake overhead:

```cpp
ConnectionPool pool;
auto conn = pool.GetConnection("https://example.com");
// Use connection
pool.ReleaseConnection(conn);
```

### 2. Health Checks

Monitor connection health:

```cpp
if (pool.IsHealthy(conn)) {
    // Connection is healthy, use it
} else {
    // Connection is unhealthy, get a new one
    conn = pool.GetConnection(host);
}
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

## Configuration

```cpp
ConnectionPoolConfig config;
config.max_connections_per_host = 10;  // Max connections per host
config.idle_timeout_ms = 30000;        // 30s idle timeout
config.health_check_interval_ms = 5000; // Check every 5s
```

## Output Example

```
Connection Lifecycle Demo
=========================

Creating connection pool...
Pool created with max 10 connections per host

Test 1: Making requests with connection reuse
Request 1: Created new connection to https://localhost:8443
Request 2: Reused existing connection (saved handshake time!)
Request 3: Reused existing connection (saved handshake time!)

Test 2: Health check
Checking connection health...
Connection is healthy ✓

Test 3: Idle timeout
Waiting for idle timeout (30s)...
Connection closed due to idle timeout

Test 4: Graceful shutdown
Shutting down connection pool...
All connections closed gracefully ✓

Demo completed!
```
