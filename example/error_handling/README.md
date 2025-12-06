# Error Handling Example

This example demonstrates best practices for handling various error scenarios in HTTP/3 applications.

## Features

- **Connection timeout handling** - Graceful timeout detection and retry logic
- **Request timeout handling** - Per-request timeout configuration
- **Stream reset handling** - Handling stream-level errors
- **Protocol error handling** - Dealing with protocol violations
- **Network interruption** - Automatic reconnection
- **Error logging** - Comprehensive error tracking

## Building

```bash
cd build
cmake ..
make error_handling_server error_handling_client
```

## Usage

### Server

Start the error handling demo server:

```bash
./bin/error_handling_server [port]

# Example:
./bin/error_handling_server 8443
```

The server provides several endpoints to simulate different error scenarios:

- `/timeout` - Simulates slow response (10s delay)
- `/error` - Returns HTTP 500 error
- `/reset` - Simulates stream reset
- `/large` - Large response to test flow control
- `/normal` - Normal successful response

### Client

Test different error scenarios:

```bash
./bin/error_handling_client <scenario> [url]

# Test connection timeout
./bin/error_handling_client timeout https://localhost:8443/timeout

# Test error response
./bin/error_handling_client error https://localhost:8443/error

# Test retry logic
./bin/error_handling_client retry https://localhost:8443/normal

# Test all scenarios
./bin/error_handling_client all https://localhost:8443
```

## Error Scenarios

### 1. Connection Timeout

```bash
./bin/error_handling_client timeout https://localhost:8443/timeout
```

Output:
```
Testing: Connection Timeout
Connecting to: https://localhost:8443/timeout
Setting timeout: 5000ms
Request timeout after 5000ms
Retrying with exponential backoff...
Attempt 2: timeout 10000ms
Attempt 3: timeout 20000ms
Max retries reached, giving up
```

### 2. Request Error Handling

```bash
./bin/error_handling_client error https://localhost:8443/error
```

Output:
```
Testing: Error Response Handling
Received HTTP 500: Internal Server Error
Error body: Simulated server error
Logging error to file: error.log
```

### 3. Retry with Exponential Backoff

```bash
./bin/error_handling_client retry https://localhost:8443/normal
```

Output:
```
Testing: Retry Logic
Attempt 1: Failed (simulated)
Waiting 1000ms before retry...
Attempt 2: Failed (simulated)
Waiting 2000ms before retry...
Attempt 3: Success!
Response: OK
```

### 4. Network Interruption

The client automatically detects network issues and attempts reconnection.

## Best Practices Demonstrated

### 1. Timeout Configuration

```cpp
Http3Config config;
config.connection_timeout_ms_ = 5000;  // 5 second connection timeout
config.idle_timeout_ms_ = 30000;       // 30 second idle timeout
```

### 2. Error Callbacks

```cpp
client->DoRequest(url, HttpMethod::kGet, request,
    [](std::shared_ptr<IResponse> response, uint32_t error) {
        if (error != 0) {
            // Handle error
            std::cerr << "Request failed: " << error << std::endl;
            return;
        }
        // Process response
    });
```

### 3. Retry Logic

```cpp
void RetryWithBackoff(int attempt, int max_attempts) {
    if (attempt >= max_attempts) {
        std::cerr << "Max retries reached" << std::endl;
        return;
    }
    
    int delay_ms = std::min(1000 * (1 << attempt), 30000);  // Cap at 30s
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    
    // Retry request
    MakeRequest(attempt + 1, max_attempts);
}
```

### 4. Error Logging

```cpp
void LogError(const std::string& error_msg) {
    std::ofstream log_file("error.log", std::ios::app);
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    log_file << std::ctime(&time_t) << ": " << error_msg << std::endl;
}
```

### 5. Graceful Degradation

```cpp
// Try HTTP/3 first, fallback to HTTP/2 if needed
if (!TryHttp3(url)) {
    std::cout << "HTTP/3 failed, falling back to HTTP/2" << std::endl;
    TryHttp2(url);
}
```

## Error Codes

Common error codes you might encounter:

- `0` - Success
- `1` - Connection timeout
- `2` - Request timeout
- `3` - Stream reset
- `4` - Protocol error
- `5` - Network error

## Tips

1. **Always set timeouts** - Prevent hanging connections
2. **Implement retry logic** - Network is unreliable
3. **Log errors** - Essential for debugging
4. **Use exponential backoff** - Avoid overwhelming the server
5. **Handle all error codes** - Don't assume success
