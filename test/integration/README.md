# P1 Core Integration Tests

This directory contains integration tests for quicX HTTP/3 implementation.

## Test Suites

### 1. HTTP/3 Methods Test (`http3_methods_test.cpp`)
Tests all HTTP methods end-to-end:
- GET, POST, PUT, DELETE, HEAD requests
- Large request bodies
- Concurrent requests

### 2. Connection Management Test (`connection_management_test.cpp`)
Tests connection lifecycle:
- Basic connection establishment
- Connection reuse
- Connection timeout
- Multiple concurrent connections

### 3. Error Handling Test (`error_handling_test.cpp`)
Tests error scenarios:
- Server errors (HTTP 500)
- Request timeouts
- Invalid URLs
- Error recovery

### 4. Stress Test (`stress_test.cpp`)
Tests system under load:
- High concurrency (50 clients, 500 requests)
- Sustained load (10 seconds continuous)
- Large data transfer (100KB+ payloads)

### 5. Advanced Features Test (`advanced_features_test.cpp`)
Tests advanced HTTP/3 features:
- Asynchronous (streaming) request/response handlers
- Request body providers (upload streaming)
- Middleware chains

### 6. Streaming & Push Test (`streaming_and_push_test.cpp`)
Tests server push and streaming:
- PUSH_PROMISE / push response delivery
- Streamed responses
- Push with path params

## Building

```bash
cd build
cmake ..
make http3_methods_test connection_management_test error_handling_test stress_test \
     advanced_features_test streaming_and_push_test
```

## Running Tests

### Run all tests
```bash
cd build
ctest
```

### Run specific test
```bash
./bin/http3_methods_test
./bin/connection_management_test
./bin/error_handling_test
./bin/stress_test
./bin/advanced_features_test
./bin/streaming_and_push_test
```

### List test cases
```bash
./bin/http3_methods_test --gtest_list_tests
```

## Test Results

All tests use Google Test framework and provide detailed output:
- ✓ PASS - Test passed
- ✗ FAIL - Test failed with details

## Notes

- Tests use embedded certificates for TLS (`test_server_helper.h`)
- Each test suite runs on a different port to avoid conflicts
- Tests are designed to be run in parallel
- Stress tests may take longer to complete
