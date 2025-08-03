# Upgrade Handlers Component Unit Tests

This directory contains unit tests for the upgrade handlers components, which provide HTTP and HTTPS protocol handling and connection management for the upgrade system.

## Test Files

### Core Component Tests

- **`connection_context_test.cpp`** - Tests for `ConnectionContext` struct
  - Connection context creation and initialization
  - State transitions (INITIAL, DETECTING, NEGOTIATING, UPGRADED, FAILED)
  - Protocol detection and target protocol setting
  - Initial data storage and ALPN protocols
  - Pending response management for partial sends
  - Negotiation timer management
  - Multiple connection contexts

- **`smart_handler_factory_test.cpp`** - Tests for `SmartHandlerFactory` class
  - Factory creation and handler instantiation
  - HTTP handler creation with various settings
  - HTTPS handler creation with certificate configuration
  - Handler selection logic (HTTP vs HTTPS preference)
  - Error handling for invalid settings
  - Multiple handler creation scenarios
  - Settings validation and fallback behavior

- **`base_smart_handler_test.cpp`** - Tests for `BaseSmartHandler` class
  - Handler creation and lifecycle management
  - Connection handling (connect, read, write, close)
  - Protocol detection and upgrade processing
  - Event driver integration
  - Timer management for negotiation timeouts
  - Response sending with partial send handling
  - Multiple connection management
  - Error handling and cleanup

- **`integration_test.cpp`** - Integration tests
  - Complete HTTP handler lifecycle
  - Complete HTTPS handler lifecycle
  - Connection context integration with handlers
  - Multiple handlers with different settings
  - Event driver integration
  - Error handling scenarios
  - Protocol negotiation workflows

## Build and Run

### Prerequisites
- CMake 3.10 or higher
- Google Test (automatically downloaded via FetchContent)
- BoringSSL (for HTTPS handler tests)
- C++11 compatible compiler

### Build
```bash
# From the project root
mkdir -p build
cd build
cmake ..
make upgrade_handlers_test
```

### Run
```bash
# Run all handlers tests
./upgrade_handlers_test

# Run specific test
./upgrade_handlers_test --gtest_filter=ConnectionContextTest.*

# Run with verbose output
./upgrade_handlers_test --gtest_verbose
```

## Test Coverage

### ConnectionContext Tests
- ✅ Context creation and initialization
- ✅ State transition management
- ✅ Protocol detection and target setting
- ✅ Data storage (initial data, ALPN protocols)
- ✅ Response management (pending response, partial sends)
- ✅ Timer management
- ✅ Multiple context handling
- ✅ Memory management and cleanup

### SmartHandlerFactory Tests
- ✅ Factory creation and instantiation
- ✅ HTTP handler creation
- ✅ HTTPS handler creation with certificates
- ✅ Handler selection logic
- ✅ Settings validation
- ✅ Error handling and fallbacks
- ✅ Multiple handler scenarios
- ✅ Port configuration handling

### BaseSmartHandler Tests
- ✅ Handler lifecycle management
- ✅ Connection event handling
- ✅ Protocol detection integration
- ✅ Upgrade process management
- ✅ Event driver integration
- ✅ Timer management
- ✅ Response sending logic
- ✅ Error handling and cleanup
- ✅ Multiple connection support

### Integration Tests
- ✅ Complete handler workflows
- ✅ Cross-component interaction
- ✅ Settings combination scenarios
- ✅ Error propagation
- ✅ Resource management
- ✅ Protocol negotiation flows

## Architecture Overview

The handlers system consists of several key components:

### ConnectionContext
- Manages connection state and protocol information
- Handles pending response data for partial sends
- Tracks negotiation timeouts and ALPN protocols

### SmartHandlerFactory
- Creates appropriate handlers based on configuration
- Handles HTTP vs HTTPS handler selection
- Validates settings and provides fallback behavior

### BaseSmartHandler
- Provides common handler functionality
- Manages connection lifecycle
- Integrates with event drivers and timers
- Handles protocol detection and upgrade processes

### HTTP/HTTPS Handlers
- Implement specific protocol handling
- Manage SSL/TLS for HTTPS connections
- Handle ALPN negotiation
- Process upgrade requests

## Mock Objects

The tests use mock objects to isolate components:

- **`MockTcpAction`**: Implements `ITcpAction` interface for testing TCP operations
- **`MockEventDriver`**: Implements `IEventDriver` interface for testing event handling
- **`TestSmartHandler`**: Concrete implementation of `BaseSmartHandler` for testing

## Notes

- Tests use `std::atomic` for thread-safe counters in mock objects
- SSL/TLS functionality is tested with mock certificates
- Event driver integration is tested without requiring actual I/O
- Connection lifecycle is thoroughly tested from creation to cleanup
- Error conditions are tested to ensure robust error handling
- Integration tests verify component interaction without requiring network connections

## Troubleshooting

### Common Issues

1. **SSL/TLS test failures**: Ensure BoringSSL is properly linked and certificates are available
2. **Mock object issues**: Verify that mock objects properly implement required interfaces
3. **Thread safety issues**: Tests use atomic variables for thread-safe operation tracking
4. **Memory leaks**: All tests properly clean up resources and verify no memory leaks

### Debug Mode

Build with debug information for better error reporting:
```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
make upgrade_handlers_test
```

### SSL Certificate Setup

For HTTPS handler tests, you may need to create test certificates:
```bash
# Create test certificate (for testing only)
openssl req -x509 -newkey rsa:4096 -keyout test.key -out test.crt -days 365 -nodes
``` 