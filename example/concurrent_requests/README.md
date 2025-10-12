# Concurrent Requests Example

This example demonstrates the **HTTP/3 multiplexing capabilities** and showcases one of the core advantages of HTTP/3 over HTTP/1.1 and HTTP/2: efficient concurrent request handling without head-of-line blocking.

## Key Features Demonstrated

### HTTP/3 Advantages
- ✅ **True Multiplexing** - Multiple requests on a single connection
- ✅ **No Head-of-Line Blocking** - Slow requests don't block fast ones
- ✅ **Connection Reuse** - Single connection for all requests
- ✅ **Parallel Processing** - Requests processed concurrently
- ✅ **Performance Optimization** - Reduced latency through parallelization

### Server Features
- ✅ **Variable Delay Endpoints** - Simulate real-world latency
  - `/fast` - 10ms delay
  - `/medium` - 100ms delay  
  - `/slow` - 500ms delay
  - `/random` - Random 10-500ms delay
- ✅ **Data Transfer** - Generate payloads of various sizes
- ✅ **Concurrent Tracking** - Monitor concurrent request levels
- ✅ **Statistics** - Real-time server metrics

### Client Features
- ✅ **Concurrent Testing** - Send multiple simultaneous requests
- ✅ **Performance Metrics** - Detailed timing and statistics
- ✅ **Timeline Visualization** - Visual request timeline
- ✅ **Efficiency Calculation** - Measure multiplexing speedup
- ✅ **Scalability Testing** - Test with increasing concurrency

## Why This Matters

### Traditional HTTP/1.1 Problem
```
Request 1 (slow: 500ms)     [===================]
Request 2 (fast: 10ms)                           [=]
Request 3 (fast: 10ms)                             [=]
Total time: ~520ms (sequential)
```

### HTTP/3 Solution
```
Request 1 (slow: 500ms)     [===================]
Request 2 (fast: 10ms)      [=]
Request 3 (fast: 10ms)       [=]
Total time: ~500ms (parallel, no blocking!)
```

## Build

### Using CMake (from project root)

```bash
mkdir -p build && cd build
cmake ..
make concurrent_server concurrent_client
```

The executables will be in `build/bin/`:
- `concurrent_server`
- `concurrent_client`

## Usage

### 1. Start the Server

```bash
./build/bin/concurrent_server
```

Output:
```
==================================
Concurrent Requests Server
==================================
Listen on: https://0.0.0.0:8885
Worker threads: 4

Endpoints:
  GET /fast        - 10ms delay
  GET /medium      - 100ms delay
  GET /slow        - 500ms delay
  GET /random      - Random delay (10-500ms)
  GET /data/:size  - Generate data (size in KB)
  GET /stats       - Statistics
==================================
```

### 2. Run the Client Tests

```bash
./build/bin/concurrent_client
```

The client runs four comprehensive tests:

#### Test 1: Mixed Concurrent Requests
- **Purpose**: Show requests don't block each other
- **Requests**: 5 fast + 5 medium + 5 slow (15 total)
- **Expected Sequential Time**: ~3050ms
- **Expected Concurrent Time**: ~500ms
- **Speedup**: ~6x

#### Test 2: Burst Request Test
- **Purpose**: Handle sudden traffic spikes
- **Requests**: 20 with random delays (10-500ms)
- **Demonstrates**: Elastic concurrency handling

#### Test 3: Concurrent Data Transfer
- **Purpose**: Multiple file downloads simultaneously
- **Sizes**: 10KB, 50KB, 100KB, 500KB, 1MB
- **Demonstrates**: Bandwidth sharing

#### Test 4: Scalability Test
- **Purpose**: Performance under increasing load
- **Levels**: 10, 25, 50, 100 concurrent requests
- **Metrics**: Throughput (req/s) at each level

## Example Output

### Request Timeline Visualization
```
========================================
CONCURRENT REQUEST RESULTS
========================================

Individual Requests:
--------------------------------------------------------------------------------
ID    Endpoint       Status    Duration(ms) Start(ms)   End(ms)     
--------------------------------------------------------------------------------
1     fast           200       12           0           12          
2     fast           200       11           1           12          
3     fast           200       13           1           14          
4     fast           200       12           2           14          
5     fast           200       11           2           13          
6     medium         200       102          3           105         
7     medium         200       101          3           104         
8     medium         200       103          4           107         
9     medium         200       101          4           105         
10    medium         200       102          5           107         
11    slow           200       502          5           507         
12    slow           200       501          6           507         
13    slow           200       503          6           509         
14    slow           200       501          7           508         
15    slow           200       502          7           509         

Statistics:
--------------------------------------------------------------------------------
Total Requests:       15
Successful Requests:  15
Failed Requests:      0
Total Wall Time:      509 ms
Sum of All Durations: 3073 ms
Min Request Duration: 11 ms
Max Request Duration: 503 ms
Avg Request Duration: 204.87 ms

Multiplexing Efficiency:
--------------------------------------------------------------------------------
Parallelization:      603.9%
Speedup Factor:       6.04x
Requests/Second:      29.47

Visualization (Timeline):
--------------------------------------------------------------------------------
  1 |=
  2 |=
  3 |==
  4 |==
  5 |==
  6 |  ==========
  7 |  ==========
  8 |   ==========
  9 |   ==========
 10 |    ==========
 11 |    =================================================
 12 |     =================================================
 13 |     ==================================================
 14 |      =================================================
 15 |      ==================================================
    |----------------------------------------------------------------------|
    0                                                                  509ms
========================================
```

### Performance Metrics

**Multiplexing Efficiency**:
- **Parallelization**: >600% (6x speedup)
- **Speedup Factor**: 6.04x
- **Requests/Second**: ~29.47

This shows that 15 requests completed in ~509ms instead of the sequential ~3073ms - a dramatic improvement!

## API Endpoints

| Method | Path | Delay | Description |
|--------|------|-------|-------------|
| GET | `/` | - | Welcome page with documentation |
| GET | `/fast` | 10ms | Fast response endpoint |
| GET | `/medium` | 100ms | Medium latency endpoint |
| GET | `/slow` | 500ms | Slow response endpoint |
| GET | `/random` | 10-500ms | Random delay endpoint |
| GET | `/data/:size` | - | Generate payload of size KB (max 1MB) |
| GET | `/stats` | - | Server statistics (requests, concurrency) |

## Code Highlights

### Server: Concurrent Request Tracking

```cpp
struct RequestStats {
    std::atomic<uint64_t> total_requests{0};
    std::atomic<uint64_t> concurrent_requests{0};
    std::atomic<uint64_t> max_concurrent{0};
    
    void IncrementConcurrent() {
        uint64_t current = ++concurrent_requests;
        // Track maximum concurrency level
        uint64_t max = max_concurrent.load();
        while (current > max && 
               !max_concurrent.compare_exchange_weak(max, current)) {
            max = max_concurrent.load();
        }
    }
};
```

### Server: Simulated Delays

```cpp
// GET /slow - Slow response (500ms)
server->AddHandler(
    quicx::http3::HttpMethod::kGet,
    "/slow",
    [stats](auto req, auto resp) {
        RAII_ConcurrentCounter counter(*stats);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        // Response...
    }
);
```

### Client: Concurrent Request Management

```cpp
class ConcurrentTester {
    void SendRequest(int id, const std::string& endpoint, const std::string& url) {
        pending_requests_++;
        auto start = std::chrono::steady_clock::now();
        
        client_->DoRequest(url, quicx::http3::HttpMethod::kGet, request,
            [this, id, endpoint, start](auto response, uint32_t error) {
                auto end = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    end - start);
                
                // Store result with timing info
                RequestResult result{id, endpoint, start, end, duration.count()};
                results_.push_back(result);
                
                pending_requests_--;
            }
        );
    }
};
```

### Client: Performance Calculation

```cpp
// Calculate multiplexing efficiency
double efficiency = (static_cast<double>(total_req_duration) / 
                     total_duration.count()) * 100.0;

double speedup = static_cast<double>(total_req_duration) / 
                 total_duration.count();

std::cout << "Parallelization: " << efficiency << "%" << std::endl;
std::cout << "Speedup Factor:  " << speedup << "x" << std::endl;
```

## Performance Insights

### HTTP/3 Multiplexing Benefits

1. **No Head-of-Line Blocking**
   - Each stream is independent
   - Packet loss affects only one stream
   - Other streams continue unaffected

2. **Connection Efficiency**
   - Single connection for all requests
   - Reduced connection overhead
   - Better resource utilization

3. **Latency Reduction**
   - Parallel request processing
   - Faster page load times
   - Improved user experience

### Real-World Scenarios

**Web Page Loading**:
- HTML, CSS, JS, images load in parallel
- Slow images don't block CSS
- Progressive rendering possible

**API Microservices**:
- Multiple API calls simultaneously
- Reduced overall response time
- Better throughput

**File Downloads**:
- Multiple files in parallel
- Efficient bandwidth usage
- No request queuing

## Testing Scenarios

### Scenario 1: Mixed Workload
```bash
# Simulates real web page with various resource types
15 requests: fast (HTML/CSS) + medium (JS) + slow (images)
Result: 6x speedup vs sequential
```

### Scenario 2: API Gateway
```bash
# Multiple microservice calls
20 random delay requests (simulating different services)
Result: Efficient handling of variable latencies
```

### Scenario 3: CDN Content Delivery
```bash
# Different file sizes downloaded concurrently
10KB to 1MB files in parallel
Result: Optimal bandwidth utilization
```

### Scenario 4: Load Testing
```bash
# Scalability under increasing load
10 → 25 → 50 → 100 concurrent requests
Result: Linear throughput scaling
```

## Comparison: HTTP/1.1 vs HTTP/3

| Metric | HTTP/1.1 | HTTP/3 |
|--------|----------|--------|
| Concurrent Requests | Limited (6 per domain) | Unlimited |
| Head-of-Line Blocking | Yes (TCP level) | No (QUIC streams) |
| Connection Overhead | High (multiple connections) | Low (single connection) |
| Request Queuing | Required | Not required |
| Latency | Higher | Lower |

## Production Considerations

### Server Optimization
- **Thread Pool Size**: Configure based on expected concurrency
- **Connection Limits**: Set appropriate max concurrent streams
- **Resource Management**: Monitor memory/CPU under load
- **Rate Limiting**: Protect against abuse

### Client Best Practices
- **Connection Pooling**: Reuse connections when possible
- **Request Prioritization**: Critical requests first
- **Timeout Management**: Set appropriate timeouts
- **Error Handling**: Retry failed requests with backoff

### Monitoring
- Track concurrent request levels
- Monitor response time distribution
- Measure throughput (req/s)
- Alert on degradation

## Troubleshooting

### Low Concurrency
- Check thread pool configuration
- Verify client/server thread count
- Review connection limits

### Poor Performance
- Measure network latency
- Check server CPU/memory
- Review request patterns

### Connection Issues
- Verify QUIC/UDP not blocked
- Check firewall settings
- Review MTU settings

## Next Steps

After understanding HTTP/3 multiplexing:
1. Explore **Stream Prioritization**
2. Implement **Request Coalescing**
3. Add **Connection Migration**
4. Build **Adaptive Streaming**

## References

- **HTTP/3 Spec**: RFC 9114
- **QUIC Protocol**: RFC 9000
- **Multiplexing**: vs HTTP/2 streams
- **Performance**: Real-world benchmarks

## License

This example is part of the QuicX project. See the main LICENSE file for details.


