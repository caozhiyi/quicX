# Performance Benchmark Tool

Measure HTTP/3 performance metrics including throughput, latency, and concurrency.

## Building

```bash
cd build && cmake .. && make performance_benchmark
```

## Usage

```bash
./bin/performance_benchmark <url> [options]

Options:
  --throughput    Measure throughput (MB/s)
  --latency       Measure request latency
  --concurrency   Test concurrent requests
  --all           Run all benchmarks (default)
```

## Example Output

```
Performance Benchmark Results
=============================

Throughput Test:
  Data transferred: 100 MB
  Time: 2.15s
  Throughput: 46.5 MB/s

Latency Test (1000 requests):
  Average: 12.3ms
  P50: 11.2ms
  P95: 18.7ms
  P99: 25.4ms

Concurrency Test:
  Max concurrent streams: 200
  Requests/second: 1250
```
