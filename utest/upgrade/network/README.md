# Upgrade Network Component Unit Tests

This directory contains unit tests for the upgrade network components, which provide cross-platform event-driven I/O multiplexing and TCP socket management.

## Test Files

### Core Component Tests

- **`tcp_socket_test.cpp`** - Tests for `TcpSocket` class
  - Socket creation and destruction
  - Socket options (non-blocking, reuse address, keep alive)
  - Handler management
  - Send/receive operations (without connection)
  - Error handling for invalid file descriptors

- **`tcp_action_test.cpp`** - Tests for `TcpAction` class
  - TCP action initialization and lifecycle
  - Listener management (add/remove listeners)
  - Timer functionality (add/remove timers)
  - Error handling for invalid operations
  - Multiple listeners and timers

- **`event_driver_test.cpp`** - Tests for `IEventDriver` interface
  - Event driver creation and initialization
  - File descriptor management (add/remove/modify)
  - Event waiting with timeout
  - Wakeup functionality
  - Platform-specific driver behavior

- **`integration_test.cpp`** - Integration tests
  - Complete TCP action lifecycle
  - TCP socket with handler integration
  - Event driver integration
  - Multiple TCP sockets
  - Multiple listeners
  - Error handling scenarios

## Build and Run

### Prerequisites
- CMake 3.10 or higher
- Google Test (automatically downloaded via FetchContent)
- C++11 compatible compiler

### Build
```bash
# From the project root
mkdir -p build
cd build
cmake ..
make upgrade_network_test
```

### Run
```bash
# Run all network tests
./upgrade_network_test

# Run specific test
./upgrade_network_test --gtest_filter=TcpSocketTest.*

# Run with verbose output
./upgrade_network_test --gtest_verbose
```

## Test Coverage

### TcpSocket Tests
- ✅ Socket creation and validity
- ✅ Socket options configuration
- ✅ Handler management
- ✅ Send/receive operations (error cases)
- ✅ File descriptor management
- ✅ Socket lifecycle

### TcpAction Tests
- ✅ Initialization and lifecycle
- ✅ Listener management
- ✅ Timer functionality
- ✅ Multiple listeners and timers
- ✅ Error handling
- ✅ Thread safety

### EventDriver Tests
- ✅ Platform-specific driver creation
- ✅ File descriptor monitoring
- ✅ Event waiting and timeout
- ✅ Wakeup mechanism
- ✅ Error handling

### Integration Tests
- ✅ Complete component interaction
- ✅ Multiple socket scenarios
- ✅ Timer and event integration
- ✅ Error propagation
- ✅ Resource management

## Platform Support

The tests are designed to work across different platforms:

- **Linux**: Uses epoll event driver
- **macOS**: Uses kqueue event driver  
- **Windows**: Uses IOCP event driver

## Mock Objects

The tests use mock objects to isolate components:

- **`MockSocketHandler`**: Implements `ISocketHandler` interface for testing socket event handling
- **Event simulation**: Uses pipes and timers to simulate real network events

## Notes

- Tests use `std::atomic` for thread-safe counters in mock objects
- Timer tests include appropriate delays to account for system scheduling
- File descriptor cleanup is handled properly in all tests
- Error conditions are tested to ensure robust error handling
- Integration tests verify component interaction without requiring actual network connections

## Troubleshooting

### Common Issues

1. **Timer tests failing**: System load may affect timer precision. Increase timeout values if needed.
2. **File descriptor limits**: Some tests create multiple sockets. Ensure adequate FD limits.
3. **Platform-specific issues**: Event driver behavior may vary slightly between platforms.

### Debug Mode

Build with debug information for better error reporting:
```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
make upgrade_network_test
``` 