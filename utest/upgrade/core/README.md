# Upgrade Core Unit Tests

This directory contains unit tests for the upgrade core components.

## Test Files

### 1. `protocol_detector_test.cpp`
Tests for the `ProtocolDetector` class:
- HTTP/1.1 protocol detection
- HTTP/2 protocol detection
- Unknown protocol handling
- Edge cases (empty data, partial requests, case sensitivity)

### 2. `version_negotiator_test.cpp`
Tests for the `VersionNegotiator` class:
- HTTP/1.1 to HTTP/3 upgrade negotiation
- HTTP/2 to HTTP/3 upgrade negotiation
- HTTP/3 direct connection
- ALPN protocol handling
- Error scenarios

### 3. `upgrade_manager_test.cpp`
Tests for the `UpgradeManager` class:
- Successful upgrade processing
- Upgrade failure handling
- Unknown protocol handling
- Response preparation

### 4. `integration_test.cpp`
Integration tests covering complete upgrade flows:
- Complete HTTP/1.1 to HTTP/3 upgrade flow
- Complete HTTP/2 to HTTP/3 upgrade flow
- HTTP/3 direct connection flow
- Unknown protocol flow
- ALPN protocol integration
- Error handling flow

## Building and Running Tests

### Prerequisites
- CMake 3.10 or higher
- Google Test (GTest)
- C++11 compatible compiler

### Build Commands
```bash
# From the project root
mkdir -p build
cd build
cmake ..
make upgrade_core_tests
```

### Run Tests
```bash
# Run all upgrade core tests
./utest/upgrade/core/upgrade_core_tests

# Run with verbose output
./utest/upgrade/core/upgrade_core_tests --gtest_verbose

# Run specific test
./utest/upgrade/core/upgrade_core_tests --gtest_filter=ProtocolDetectorTest.*

# Run with XML output
./utest/upgrade/core/upgrade_core_tests --gtest_output=xml:test_results.xml
```

## Test Coverage

The tests cover:

### Protocol Detection
- ✅ HTTP/1.1 GET/POST requests
- ✅ HTTP/1.1 with various headers
- ✅ HTTP/2 connection preface
- ✅ HTTP/2 settings frames
- ✅ Unknown protocols
- ✅ Empty/incomplete data
- ✅ Case sensitivity
- ✅ Whitespace handling

### Version Negotiation
- ✅ HTTP/1.1 to HTTP/3 upgrade
- ✅ HTTP/2 to HTTP/3 upgrade
- ✅ HTTP/3 direct connection
- ✅ ALPN protocol negotiation
- ✅ Upgrade headers processing
- ✅ Error handling
- ✅ Invalid settings

### Upgrade Management
- ✅ Successful upgrade processing
- ✅ Upgrade failure handling
- ✅ Response preparation
- ✅ Multiple upgrade attempts
- ✅ Different settings configurations
- ✅ Error scenarios

### Integration
- ✅ Complete upgrade flows
- ✅ Component interaction
- ✅ Error propagation
- ✅ Resource management

## Mock Objects

The tests use mock objects to isolate the components under test:

- `MockTcpSocket`: Implements `ITcpSocket` interface for testing
- Provides controlled behavior for send/receive operations
- Tracks socket state and operations

## Test Data

Test data includes:
- Realistic HTTP/1.1 requests
- HTTP/2 connection prefaces
- ALPN protocol lists
- Various error conditions
- Edge cases and boundary conditions

## Contributing

When adding new tests:
1. Follow the existing naming conventions
2. Use descriptive test names
3. Include both positive and negative test cases
4. Test edge cases and error conditions
5. Update this README if adding new test categories 