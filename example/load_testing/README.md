# Load Testing Tool

Stress test HTTP/3 servers with configurable load patterns.

## Features

- **Configurable load** - Control number of clients and requests
- **Ramp-up time** - Gradual load increase
- **Real-time metrics** - Monitor performance during test
- **Detailed reports** - Comprehensive test results

## Building

```bash
cd build && cmake .. && make load_tester
```

## Usage

```bash
./bin/load_tester <url> [options]

Options:
  --clients <N>      Number of concurrent clients (default: 10)
  --requests <N>     Requests per client (default: 100)
  --rampup <seconds> Ramp-up time (default: 0)
  --duration <seconds> Test duration (default: 60)
```

## Examples

### Basic Load Test

```bash
./bin/load_tester https://localhost:8443/hello --clients 50 --requests 100
```

### Sustained Load Test

```bash
./bin/load_tester https://localhost:8443/api --clients 100 --duration 300
```

### Gradual Ramp-up

```bash
./bin/load_tester https://localhost:8443/data --clients 200 --rampup 30
```

## Example Output

```
Load Testing Tool
=================
Target: https://localhost:8443/hello
Clients: 100
Requests per client: 100
Total requests: 10000

Starting load test...
[=====>              ] 30% | 3000/10000 | 250 req/s | Avg: 15ms
[==========>         ] 60% | 6000/10000 | 280 req/s | Avg: 14ms
[===============>    ] 90% | 9000/10000 | 295 req/s | Avg: 13ms
[====================] 100% | 10000/10000 | 300 req/s | Avg: 13ms

Test Results:
  Total requests: 10000
  Successful: 9998 (99.98%)
  Failed: 2 (0.02%)
  Duration: 33.5s
  Throughput: 298.5 req/s
  
Latency:
  Average: 13.2ms
  P50: 12.5ms
  P95: 18.9ms
  P99: 25.3ms
  Min: 8.1ms
  Max: 45.2ms
```
