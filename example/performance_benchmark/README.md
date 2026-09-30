# Performance Benchmark Tool

Measure HTTP/3 performance metrics including latency, throughput, and concurrency.

## Building

```bash
cd build && cmake .. && make performance_benchmark
```

## Usage

```bash
./bin/performance_benchmark <url>

# Example:
./bin/performance_benchmark https://localhost:7001/hello
```

The tool takes a single URL and runs three benchmarks in sequence:
latency (100 requests), throughput (large download), concurrency (50 parallel requests).

## Example Output

```
╔════════════════════════════════════════╗
║  Performance Benchmark                 ║
╚════════════════════════════════════════╝

Latency Benchmark
========================================
Running 100 requests...

Results:
  Requests: 100
  Average: 12.30ms
  P50: 11.20ms
  P95: 18.70ms
  P99: 25.40ms
  Min: 9.80ms
  Max: 31.20ms


Throughput Benchmark
========================================
Downloading data...

Results:
  Data transferred: 100.00 MB
  Time: 2.15s
  Throughput: 46.51 MB/s


Concurrency Benchmark
========================================
Sending 50 concurrent requests...

Results:
  Concurrent requests: 50
  Total time: 0.04s
  Requests/second: 1250.00

Benchmark completed!
```
