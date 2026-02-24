# Concurrent Requests Example

Demonstrates HTTP/3 multiplexing with multiple concurrent requests over a single QUIC connection.

## Features

- **Stream Multiplexing** - Send multiple requests simultaneously over one connection
- **Request Statistics** - Track timing, success rates, and throughput
- **Timeline Visualization** - Visual representation of concurrent request execution
- **Simulated Delays** - Server simulates fast/medium/slow endpoints for testing

## Building

```bash
cd build && cmake .. && make concurrent_server concurrent_client
```

## Usage

### Server

```bash
./bin/concurrent_server [port]
```

The server provides three endpoints with different response times:
- `/api/fast` - Responds immediately
- `/api/medium` - 100ms simulated delay  
- `/api/slow` - 200ms simulated delay
- `/api/stats` - Server statistics in JSON format

### Client

```bash
./bin/concurrent_client <server_url>

# Example:
./bin/concurrent_client https://localhost:7003
```

## How It Works

1. Client establishes a single QUIC connection to the server
2. Sends 15 concurrent requests (5 each to fast/medium/slow endpoints)
3. All requests are multiplexed over the same connection
4. Results show timing and parallelization efficiency

## Example Output

```
Concurrent Request Test
=======================
Sending 15 concurrent requests...

========================================
CONCURRENT REQUEST RESULTS
========================================

Individual Requests:
--------------------------------------------------------------------------------
ID    Endpoint       Status    Duration(ms) Start(ms)   End(ms)
--------------------------------------------------------------------------------
1     /api/fast      200       15          0           15
2     /api/fast      200       18          0           18
3     /api/medium    200       115         0           115
4     /api/slow      200       220         0           220
...

Statistics:
--------------------------------------------------------------------------------
Total Requests:       15
Successful Requests:  15
Failed Requests:      0
Total Wall Time:      225 ms
Sum of All Durations: 1850 ms
Min Request Duration: 15 ms
Max Request Duration: 220 ms
Avg Request Duration: 123.33 ms

Multiplexing Efficiency:
--------------------------------------------------------------------------------
Parallelization:      822.2%
Speedup Factor:       8.22x
Requests/Second:      66.67
```

## Use Cases

- Load testing HTTP/3 servers
- Demonstrating QUIC multiplexing benefits
- Benchmarking concurrent request handling
- Testing stream prioritization
