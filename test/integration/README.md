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

## Building

```bash
cd build
cmake ..
make http3_methods_test connection_management_test error_handling_test stress_test
```

## Running Tests

### Run all tests
```bash
cd build
ctest
```

### Run specific test
```bash
./bin/test/http3_methods_test
./bin/test/connection_management_test
./bin/test/error_handling_test
./bin/test/stress_test
```

### Run with verbose output
```bash
./bin/test/http3_methods_test --gtest_verbose
```

## Test Results

All tests use Google Test framework and provide detailed output:
- ✓ PASS - Test passed
- ✗ FAIL - Test failed with details

## Notes

- Tests use embedded certificates for TLS
- Each test suite runs on a different port to avoid conflicts
- Tests are designed to be run in parallel
- Stress tests may take longer to complete
