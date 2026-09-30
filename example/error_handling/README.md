# Error Handling Example

This example demonstrates best practices for handling various error scenarios in HTTP/3 applications.

## Features

- **Connection timeout handling** - Timeout detection via `connection_timeout_ms_`
- **Request timeout handling** - Per-request timeout configuration
- **Error response handling** - Handling HTTP error status codes
- **Large response handling** - 1MB payload through flow control
- **Retry logic** - Exponential backoff retries
- **Error logging** - Logging failures to `error.log`

## Building

```bash
cd build
cmake ..
make error_handling_server error_handling_client
```

## Usage

### Server

Start the error handling demo server (listens on port 7005 by default):

```bash
./bin/error_handling_server [port]

# Example:
./bin/error_handling_server 7005
```

The server provides several endpoints to simulate different error scenarios:

- `/normal` - Normal successful response
- `/timeout` - Simulates slow response (10s delay)
- `/error` - Returns HTTP 500 error
- `/large` - 1MB response to test flow control

### Client

Test different error scenarios (`[url]` is the server base URL, defaults to `https://localhost:7005`):

```bash
./bin/error_handling_client <scenario> [url]

# Test connection timeout
./bin/error_handling_client timeout https://localhost:7005

# Test error response
./bin/error_handling_client error https://localhost:7005

# Test retry logic
./bin/error_handling_client retry https://localhost:7005

# Test large response
./bin/error_handling_client large https://localhost:7005

# Test all scenarios
./bin/error_handling_client all https://localhost:7005
```

## Error Scenarios

### 1. Connection Timeout

```bash
./bin/error_handling_client timeout https://localhost:7005
```

Output:
```
========================================
Test 1: Connection Timeout
========================================
Requesting: https://localhost:7005/timeout
Timeout set to: 5000ms
Server will delay 10s, expect timeout...
✓ Request timed out as expected after 5003ms
  Error code: 11
```

### 2. Request Error Handling

```bash
./bin/error_handling_client error https://localhost:7005
```

Output:
```
========================================
Test 2: Error Response Handling
========================================
Requesting: https://localhost:7005/error
✗ Received HTTP 500
  Body: Simulated server error
  Logged to error.log
```

### 3. Retry with Exponential Backoff

```bash
./bin/error_handling_client retry https://localhost:7005
```

The `TestRetryLogic` scenario requests `/error` up to 3 times with exponential
backoff between attempts, logging each failure to `error.log`.

### 4. Network Interruption

The client detects failed requests through the `error` code passed to the
`DoRequest` callback and can trigger reconnection / retry logic accordingly.

## Best Practices Demonstrated

### 1. Timeout Configuration

```cpp
quicx::Http3ClientConfig config;
config.quic_config_.verify_peer_ = false;   // examples use self-signed certs
config.connection_timeout_ms_ = 5000;       // 5 second connection timeout
```

### 2. Error Callbacks

```cpp
client->DoRequest(url, quicx::HttpMethod::kGet, request,
    [](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
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

## Error Codes

`error != 0` in the `DoRequest` callback indicates a transport/protocol failure
(timeout, connection loss, stream reset, etc. — see the error definitions in
`src/common/error.h`); otherwise inspect `response->GetStatusCode()` for
HTTP-level errors.

## Tips

1. **Always set timeouts** - Prevent hanging connections
2. **Implement retry logic** - Network is unreliable
3. **Log errors** - Essential for debugging
4. **Use exponential backoff** - Avoid overwhelming the server
5. **Handle all error codes** - Don't assume success
