# Network component unit tests
set(NETWORK_TEST_SOURCES
    main_test.cpp
    tcp_socket_test.cpp
    tcp_action_test.cpp
    event_driver_test.cpp
    integration_test.cpp
)

# Create test executable
add_executable(upgrade_network_test ${NETWORK_TEST_SOURCES})

# Link against required libraries
target_link_libraries(upgrade_network_test
    gtest
    gtest_main
    pthread
)

# Include directories
target_include_directories(upgrade_network_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third
)

# Add test to CTest
add_test(NAME UpgradeNetworkTest COMMAND upgrade_network_test)

# Set test properties
set_tests_properties(UpgradeNetworkTest PROPERTIES
    TIMEOUT 300
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR}/lib"
) 