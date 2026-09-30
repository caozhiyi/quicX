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
./bin/concurrent_server
```

The server listens on port 7003 and provides endpoints with different response times:
- `/fast` - 5ms simulated delay
- `/medium` - 20ms simulated delay
- `/slow` - 50ms simulated delay
- `/random` - Random delay
- `/data/:size` - Payload of the given size
- `/stats` - Server statistics in JSON format

### Client

```bash
./bin/concurrent_client
```

The client connects to `https://127.0.0.1:7003` by default and runs 4 test rounds (burst of 15, mixed load of 20, stress of 5, escalating concurrency of 10/25/50).

## How It Works

1. Client establishes a single QUIC connection to the server
2. Sends batches of concurrent requests (first round: 15 requests across fast/medium/slow endpoints)
3. All requests are multiplexed over the same connection
4. Results show timing and parallelization efficiency

## Example Output

```
Concurrent Request Test
=======================
Sending 15 requests concurrently:

========================================
CONCURRENT REQUEST RESULTS
========================================

Individual Requests:
--------------------------------------------------------------------------------
ID    Endpoint       Status    Duration(ms) Start(ms)   End(ms)
--------------------------------------------------------------------------------
1     fast           200       8           0           8
2     fast           200       9           0           9
3     medium         200       24          0           24
4     slow           200       53          0           53
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
